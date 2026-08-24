// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for what a character is wearing. Issue #828.
 *
 * WHAT IS ACTUALLY WORTH GUARDING HERE, and it is not "an item goes in a slot".
 * Three things:
 *
 *   **A worn item's stats reach the character.** That is the whole reason this
 *   component exists. `UCataclysmItemModifiers::ModifiersFor` had been built and
 *   tested for weeks with no caller anywhere outside the automation tests, so
 *   "the parts work" was already true and meant nothing.
 *
 *   **Nothing is destroyed or duplicated.** An equip that replaces something has
 *   to hand back what came off, exactly as `UCataclysmInventoryComponent::
 *   AddItem` answers where an item went. A caller that ignores the answer loses
 *   an item, and nothing else would notice.
 *
 *   **A two-handed weapon is counted once.** It occupies both weapon slots, and
 *   the obvious way to represent that -- storing it in both -- would double
 *   every affix on it. The item is stored in Weapon1 alone and Weapon2 is left
 *   empty, so this is the assertion that stops somebody "fixing" that later.
 *
 * NOTHING HERE NEEDS A WORLD OR A RENDERER. Unlike the particle effects, which
 * issue #559 says no test can see, every rule in this file is an ordinary value
 * check, so all of it is covered.
 *
 * THE ITEM BASES ARE REAL NAMES FROM `game/Data/ItemBases.csv`, not invented
 * ones, because `Equip` looks the base up to find out which slot the item
 * belongs in. An invented base is refused as not being an item, which is itself
 * checked below.
 */
namespace CataclysmEquipmentTest
{
	/** Real base names. A base this file names that leaves the data fails
	 *  ItsBaseNamesAreRealRowsInTheItemBasesTable below, rather than making
	 *  every other test in the file quietly assert nothing. */
	const TCHAR* HeadBase = TEXT("Head_Helm");
	const TCHAR* RingBase = TEXT("Ring_Band");
	const TCHAR* BootsBase = TEXT("Boots_Sabatons");
	const TCHAR* BeltBase = TEXT("Belt_Girdle");
	const TCHAR* OneHandedBase = TEXT("Weapon_Sword");
	const TCHAR* OtherOneHandedBase = TEXT("Weapon_Dagger");
	const TCHAR* TwoHandedBase = TEXT("Weapon_Greatsword");

	/** A real affix granting flat maximum health, top value 120. */
	const TCHAR* HealthAffix = TEXT("Stat_Flat_maximum_health");
	const TCHAR* HealthStat = TEXT("max_health");

	UCataclysmEquipmentComponent* MakeEquipment()
	{
		return NewObject<UCataclysmEquipmentComponent>(GetTransientPackage());
	}

	/** An item of a named base, with no affixes. */
	FCataclysmItem Plain(const TCHAR* Base, int32 GearLevel = 0)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		return Item;
	}

	/** An item carrying one perfectly rolled top-tier flat health affix. */
	FCataclysmItem WithHealthAffix(const TCHAR* Base)
	{
		FCataclysmItem Item = Plain(Base);

		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(HealthAffix);
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);

		return Item;
	}

	/**
	 * An actor carrying all five attribute sets, and its world.
	 *
	 * IT NEEDS A REAL ACTOR AND THERE IS NO WAY ROUND IT.
	 * `InitAbilityActorInfo(nullptr, nullptr)` does not fail politely: it
	 * asserts `AbilityActorInfo.IsValid()` and takes the whole automation run
	 * down with it, so the run reports "unknown tests performed" and every
	 * later test in the suite never runs. That is what the first version of
	 * this file did.
	 *
	 * ALL FIVE SETS RATHER THAN THE ONE THE TEST READS, because
	 * UCataclysmPlayerClassStats::ApplyTo walks all twenty class stats and
	 * skips any whose set is missing. A character holding only the vital set
	 * would exercise a code path no real character ever takes.
	 */
	struct FScopedCharacter
	{
		explicit FScopedCharacter(UWorld* InWorld)
		{
			Actor = InWorld->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject is a
			// template and deduces T from the argument, so a TObjectPtr would
			// deduce the wrapper rather than the attribute set.
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmPrimaryAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Equipment = NewObject<UCataclysmEquipmentComponent>(Actor);
			Equipment->RegisterComponent();
		}

		~FScopedCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/** How many modifiers a stat has in a gathered set. */
	int32 CountFor(const TMap<FName, TArray<FCataclysmStatModifier>>& Modifiers,
				   const TCHAR* Stat)
	{
		const TArray<FCataclysmStatModifier>* Found = Modifiers.Find(FName(Stat));
		return Found ? Found->Num() : 0;
	}
}

// ---------------------------------------------------------------------------
// The slots themselves
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearSlotsMatchTheItemData,
	"Cataclysm.Equipment.EverySlotNameMatchesAnItemBaseSlotValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearSlotsMatchTheItemData::RunTest(const FString& Parameters)
{
	// THE ENUM IS THE THIRD PLACE THE SLOT LIST IS WRITTEN DOWN. The design
	// document's Item Slots section is the first, the Slot column of
	// game/Data/ItemBases.csv is the second, and ECataclysmGearSlot is this one.
	// A slot in the enum that no item base can name is a place nothing can ever
	// be put, and it would look exactly like an empty slot.
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!BaseTable)
	{
		AddError(TEXT("The item bases table could not be loaded."));
		return false;
	}

	TSet<FString> SlotsInData;
	for (const TPair<FName, uint8*>& Row : BaseTable->GetRowMap())
	{
		const FCataclysmItemBaseRow* Base =
			reinterpret_cast<const FCataclysmItemBaseRow*>(Row.Value);
		if (Base && !Base->Slot.IsEmpty())
		{
			SlotsInData.Add(Base->Slot);
		}
	}

	if (SlotsInData.Num() == 0)
	{
		AddError(TEXT("No item base names a slot, so this test checked nothing."));
		return false;
	}

	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		const FString Wanted = UCataclysmGearSlots::BaseSlotFor(Slot);
		if (!SlotsInData.Contains(Wanted))
		{
			AddError(FString::Printf(
				TEXT("The gear slot %s expects item bases whose Slot column reads "
					 "'%s', and no row in game/Data/ItemBases.csv does. Nothing "
					 "could ever be worn there."),
				*UCataclysmGearSlots::DisplayName(Slot), *Wanted));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearSlotBaseNamesAreReal,
	"Cataclysm.Equipment.ItsBaseNamesAreRealRowsInTheItemBasesTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearSlotBaseNamesAreReal::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	// WITHOUT THIS EVERY OTHER TEST IN THE FILE COULD PASS WHILE ASSERTING
	// NOTHING. Equip refuses an item whose base is not in the table, so a base
	// renamed in the data would turn "it went into the right slot" into "it was
	// refused", and several tests below would still be satisfied by the refusal
	// unless they check the result, which is easy to forget in one of them.
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!BaseTable)
	{
		AddError(TEXT("The item bases table could not be loaded."));
		return false;
	}

	for (const TCHAR* Base : {HeadBase, RingBase, BootsBase, BeltBase,
							  OneHandedBase, OtherOneHandedBase, TwoHandedBase})
	{
		if (!BaseTable->FindRow<FCataclysmItemBaseRow>(
				FName(Base), TEXT("test"), /*bWarnIfMissing=*/false))
		{
			AddError(FString::Printf(
				TEXT("%s is not a row in game/Data/ItemBases.csv. The tests in "
					 "CataclysmEquipmentTests.cpp are built on it."), Base));
		}
	}

	// The affix these tests roll has to be real for the same reason.
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	if (AffixTable && !AffixTable->FindRow<FCataclysmAffixRow>(
			FName(HealthAffix), TEXT("test"), /*bWarnIfMissing=*/false))
	{
		AddError(FString::Printf(
			TEXT("%s is not a row in game/Data/Affixes.csv."), HealthAffix));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearSlotCandidates,
	"Cataclysm.Equipment.ARingFitsEightSlotsAndAHelmFitsOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearSlotCandidates::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a ring may go in any of the eight ring slots"),
		UCataclysmGearSlots::CandidateSlotsFor(TEXT("Ring")).Num(), 8);

	TestEqual(TEXT("a weapon may go in either hand"),
		UCataclysmGearSlots::CandidateSlotsFor(TEXT("Weapon")).Num(), 2);

	const TArray<ECataclysmGearSlot> Head =
		UCataclysmGearSlots::CandidateSlotsFor(TEXT("Head"));
	if (TestEqual(TEXT("a helm has exactly one place to go"), Head.Num(), 1))
	{
		TestTrue(TEXT("and that place is the head"),
			Head[0] == ECataclysmGearSlot::Head);
	}

	// A POTION NAMES A SLOT THIS ENUM DELIBERATELY DOES NOT CARRY. The header
	// says why the four potion slots are absent; this is the behaviour that
	// follows, and it is "nowhere to put it" rather than an error.
	TestEqual(TEXT("a slot value this character does not have fits nowhere"),
		UCataclysmGearSlots::CandidateSlotsFor(TEXT("Potion")).Num(), 0);

	TestEqual(TEXT("and neither does an empty one"),
		UCataclysmGearSlots::CandidateSlotsFor(FString()).Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Putting things on and taking them off
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEquipPutsAnItemInItsOwnSlot,
	"Cataclysm.Equipment.AnItemGoesInTheSlotItsBaseNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEquipPutsAnItemInItsOwnSlot::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	TestEqual(TEXT("a character starts wearing nothing"),
		Equipment->NumEquipped(), 0);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	const ECataclysmEquipResult Result =
		Equipment->Equip(Plain(HeadBase), Removed, AlsoRemoved, Slot);

	TestTrue(TEXT("a helm is equipped"),
		Result == ECataclysmEquipResult::Equipped);
	TestTrue(TEXT("and it went on the head"), Slot == ECataclysmGearSlot::Head);
	TestTrue(TEXT("and nothing came off"), Removed.Base.IsNone());
	TestEqual(TEXT("and the character is wearing one thing"),
		Equipment->NumEquipped(), 1);

	const FCataclysmItem* Worn = Equipment->EquippedAt(ECataclysmGearSlot::Head);
	if (TestNotNull(TEXT("the head slot holds something"), Worn))
	{
		TestEqual(TEXT("and it is the helm"), Worn->Base, FName(HeadBase));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEquipRefusesTheWrongSlot,
	"Cataclysm.Equipment.AHelmIsRefusedByTheBootSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEquipRefusesTheWrongSlot::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	const ECataclysmEquipResult Result = Equipment->EquipInto(
		Plain(HeadBase), ECataclysmGearSlot::Boots, Removed, AlsoRemoved);

	TestTrue(TEXT("a helm does not go on the feet"),
		Result == ECataclysmEquipResult::WrongSlotForThisItem);
	TestEqual(TEXT("and nothing was worn as a result"),
		Equipment->NumEquipped(), 0);

	// AN ITEM WITH NO BASE IS NOT AN ITEM, and the distinction matters because
	// an empty FCataclysmItem is exactly what an empty inventory slot holds.
	// Equipping one would put "nothing" on and report success.
	FCataclysmItem Nothing;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	TestTrue(TEXT("an empty slot's contents cannot be worn"),
		Equipment->Equip(Nothing, Removed, AlsoRemoved, Slot)
			== ECataclysmEquipResult::NotAnItem);

	// A base that is not in the table cannot be judged, so it is refused rather
	// than guessed at.
	TestTrue(TEXT("an item whose base is not in the data cannot be worn"),
		Equipment->Equip(Plain(TEXT("NotARealBase")), Removed, AlsoRemoved, Slot)
			== ECataclysmEquipResult::NotAnItem);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRingsFillTheEightSlots,
	"Cataclysm.Equipment.EightRingsFillEightSlotsAndTheNinthReplacesOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRingsFillTheEightSlots::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	// EACH RING FINDS A FREE SLOT RATHER THAN REPLACING THE LAST. Filling an
	// empty ring slot is what a player means by "wear this"; a version that
	// always used the first slot would leave a character able to wear one ring
	// while showing eight places to put one.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		const ECataclysmEquipResult Result =
			Equipment->Equip(Plain(RingBase, Index), Removed, AlsoRemoved, Slot);

		TestTrue(FString::Printf(TEXT("ring %d goes on"), Index + 1),
			Result == ECataclysmEquipResult::Equipped);
		TestTrue(FString::Printf(TEXT("ring %d went in a ring slot"), Index + 1),
			UCataclysmGearSlots::IsRingSlot(Slot));
		TestTrue(FString::Printf(TEXT("ring %d displaced nothing"), Index + 1),
			Removed.Base.IsNone());
	}

	TestEqual(TEXT("eight rings are worn"), Equipment->NumEquipped(), 8);

	// THE NINTH HAS NOWHERE FREE, SO IT REPLACES, AND WHAT CAME OFF IS HANDED
	// BACK. A version that refused would be defensible; one that replaced
	// silently would destroy a ring.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	const ECataclysmEquipResult Ninth =
		Equipment->Equip(Plain(RingBase, 9), Removed, AlsoRemoved, Slot);

	TestTrue(TEXT("the ninth ring replaces one"),
		Ninth == ECataclysmEquipResult::Swapped);
	TestFalse(TEXT("and the one it replaced is handed back"),
		Removed.Base.IsNone());
	TestEqual(TEXT("so the character still wears eight"),
		Equipment->NumEquipped(), 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnequipHandsTheItemBack,
	"Cataclysm.Equipment.TakingSomethingOffHandsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUnequipHandsTheItemBack::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Plain(HeadBase, 5), Removed, AlsoRemoved, Slot);

	FCataclysmItem TakenOff;
	TestTrue(TEXT("the helm comes off"),
		Equipment->Unequip(ECataclysmGearSlot::Head, TakenOff));
	TestEqual(TEXT("and it is the helm that was on"),
		TakenOff.Base, FName(HeadBase));
	TestEqual(TEXT("with its upgrade level intact"), TakenOff.GearLevel, 5);
	TestEqual(TEXT("and the character wears nothing"),
		Equipment->NumEquipped(), 0);

	// TAKING SOMETHING OFF AN EMPTY SLOT ANSWERS FALSE RATHER THAN HANDING BACK
	// AN EMPTY ITEM THAT LOOKS REAL. A caller putting the result in the bag
	// would otherwise add a phantom.
	FCataclysmItem Nothing;
	TestFalse(TEXT("an empty slot has nothing to take off"),
		Equipment->Unequip(ECataclysmGearSlot::Head, Nothing));

	return true;
}

// ---------------------------------------------------------------------------
// Two-handed weapons
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoHandedTakesBothHands,
	"Cataclysm.Equipment.ATwoHandedWeaponTakesBothHandsAndIsStoredOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTwoHandedTakesBothHands::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;

	// Both hands start full, so both have to come off.
	Equipment->Equip(Plain(OneHandedBase), Removed, AlsoRemoved, Slot);
	Equipment->Equip(Plain(OtherOneHandedBase), Removed, AlsoRemoved, Slot);
	TestEqual(TEXT("two one-handed weapons fill both hands"),
		Equipment->NumEquipped(), 2);

	const ECataclysmEquipResult Result = Equipment->EquipInto(
		Plain(TwoHandedBase), ECataclysmGearSlot::Weapon2, Removed, AlsoRemoved);

	TestTrue(TEXT("the greatsword replaces what was held"),
		Result == ECataclysmEquipResult::Swapped);
	TestFalse(TEXT("and the first hand's weapon is handed back"),
		Removed.Base.IsNone());
	TestFalse(TEXT("and so is the second hand's"),
		AlsoRemoved.Base.IsNone());

	// STORED IN Weapon1 EVEN THOUGH Weapon2 WAS ASKED FOR, because the two
	// slots are interchangeable -- the design says there is no primary hand.
	const FCataclysmItem* First = Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
	if (TestNotNull(TEXT("the first weapon slot holds it"), First))
	{
		TestEqual(TEXT("and it is the greatsword"),
			First->Base, FName(TwoHandedBase));
	}

	// THE SECOND SLOT IS EMPTY RATHER THAN HOLDING A SECOND COPY. This is the
	// assertion that stops the weapon's affixes being counted twice.
	TestNull(TEXT("the second weapon slot holds nothing"),
		Equipment->EquippedAt(ECataclysmGearSlot::Weapon2));
	TestEqual(TEXT("so the character is wearing one thing, not two"),
		Equipment->NumEquipped(), 1);
	TestTrue(TEXT("and it is known to be filling both hands"),
		Equipment->TwoHandedOccupiesBothWeaponSlots());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoHandedHandsBackBoth,
	"Cataclysm.Equipment.ATwoHandedWeaponHandsBackBothWeaponsItReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTwoHandedHandsBackBoth::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	// THIS IS A DESTROYED ITEM, NOT AN UNTIDY RETURN VALUE. One thing goes on
	// and two come off, and a caller can only put back what it is handed. The
	// first version of Equip called EquipInto with a local variable for the
	// second displaced item and threw the local away, so putting a greatsword on
	// over a sword and a dagger silently deleted one of them. Nothing else in the
	// project would have noticed: the weapon simply would not be in the bag.
	//
	// EquipInto was always right. Equip, the one that picks the slot for you, was
	// not, and it is the one an interface calls.
	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;

	Equipment->Equip(Plain(OneHandedBase), Removed, AlsoRemoved, Slot);
	Equipment->Equip(Plain(OtherOneHandedBase), Removed, AlsoRemoved, Slot);
	TestEqual(TEXT("both hands are full"), Equipment->NumEquipped(), 2);

	FCataclysmItem First;
	FCataclysmItem Second;
	const ECataclysmEquipResult Result =
		Equipment->Equip(Plain(TwoHandedBase), First, Second, Slot);

	TestTrue(TEXT("the greatsword goes on"),
		Result == ECataclysmEquipResult::Swapped);
	TestEqual(TEXT("and it went into the first weapon slot"),
		static_cast<int32>(Slot),
		static_cast<int32>(ECataclysmGearSlot::Weapon1));

	// BOTH, AND NAMED. Checking only that two non-empty items came back would
	// pass if the same weapon were handed over twice.
	TArray<FName> HandedBack;
	if (!First.Base.IsNone())
	{
		HandedBack.Add(First.Base);
	}
	if (!Second.Base.IsNone())
	{
		HandedBack.Add(Second.Base);
	}

	TestEqual(TEXT("two weapons are handed back"), HandedBack.Num(), 2);
	TestTrue(TEXT("the sword is one of them"),
		HandedBack.Contains(FName(OneHandedBase)));
	TestTrue(TEXT("and the dagger is the other"),
		HandedBack.Contains(FName(OtherOneHandedBase)));

	TestEqual(TEXT("leaving only the greatsword worn"),
		Equipment->NumEquipped(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoHandedAffixesCountOnce,
	"Cataclysm.Equipment.ATwoHandedWeaponsAffixesAreCountedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTwoHandedAffixesCountOnce::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* OneHander = MakeEquipment();
	UCataclysmEquipmentComponent* TwoHander = MakeEquipment();

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	OneHander->Equip(WithHealthAffix(OneHandedBase), Removed, AlsoRemoved, Slot);
	TwoHander->Equip(WithHealthAffix(TwoHandedBase), Removed, AlsoRemoved, Slot);

	const int32 OneHanded = CountFor(OneHander->GatherModifiers(), HealthStat);
	const int32 TwoHanded = CountFor(TwoHander->GatherModifiers(), HealthStat);

	TestTrue(TEXT("a one-handed weapon's health affix is gathered"),
		OneHanded > 0);

	// ONE MODIFIER, NOT TWO. A two-handed weapon is worth double, and that
	// doubling comes from UCataclysmItemValues::TwoHandedMultiplier inside the
	// value rather than from the item being gathered twice. If this ever reads
	// two, somebody has stored the weapon in both slots.
	TestEqual(TEXT("and a two-handed weapon's is gathered exactly as often"),
		TwoHanded, OneHanded);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOneHandedReplacesATwoHander,
	"Cataclysm.Equipment.AOneHandedWeaponTakesOffATwoHandedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOneHandedReplacesATwoHander::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Plain(TwoHandedBase), Removed, AlsoRemoved, Slot);

	// THE TWO-HANDER COMES OFF RATHER THAN THE EQUIP BEING REFUSED. It occupies
	// both hands, so there is no free hand; refusing would leave the player
	// unable to change weapon at all without an explicit unequip they have no
	// reason to know they need.
	const ECataclysmEquipResult Result = Equipment->EquipInto(
		Plain(OneHandedBase), ECataclysmGearSlot::Weapon2, Removed, AlsoRemoved);

	TestTrue(TEXT("the sword goes on"),
		Result == ECataclysmEquipResult::Swapped);
	TestEqual(TEXT("and the greatsword is handed back"),
		Removed.Base, FName(TwoHandedBase));
	TestFalse(TEXT("and no longer fills both hands"),
		Equipment->TwoHandedOccupiesBothWeaponSlots());
	TestEqual(TEXT("leaving one weapon worn"), Equipment->NumEquipped(), 1);

	return true;
}

// ---------------------------------------------------------------------------
// What it is all for: the stats
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGatherModifiersSumsWhatIsWorn,
	"Cataclysm.Equipment.WornItemsGrantModifiersAndBareOnesGrantNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatherModifiersSumsWhatIsWorn::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	TestEqual(TEXT("a character wearing nothing grants no health"),
		CountFor(Equipment->GatherModifiers(), HealthStat), 0);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(WithHealthAffix(HeadBase), Removed, AlsoRemoved, Slot);

	const int32 AfterOne = CountFor(Equipment->GatherModifiers(), HealthStat);
	TestTrue(TEXT("a helm with a health affix grants one"), AfterOne > 0);

	// TWO PIECES GRANT MORE THAN ONE, which is what AccumulateInto is for and
	// is the case a single-item test cannot see.
	Equipment->Equip(WithHealthAffix(BootsBase), Removed, AlsoRemoved, Slot);
	TestEqual(TEXT("and boots with the same affix add to it"),
		CountFor(Equipment->GatherModifiers(), HealthStat), AfterOne * 2);

	// TAKING IT OFF TAKES THE MODIFIER WITH IT. A version that cached the
	// gathered set and never rebuilt it would pass every test above and fail
	// this one.
	Equipment->Unequip(ECataclysmGearSlot::Head, Removed);
	TestEqual(TEXT("taking the helm off removes its share"),
		CountFor(Equipment->GatherModifiers(), HealthStat), AfterOne);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWornGearChangesTheCharacter,
	"Cataclysm.Equipment.WearingAnItemRaisesTheAttributeAndRemovingItLowersIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWornGearChangesTheCharacter::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	// THIS IS THE TEST THE WHOLE COMPONENT EXISTS FOR. Everything above is about
	// items going in the right holes; this is whether wearing one does anything.
	// Before issue #828 the answer was no, and every part needed to make it yes
	// was already built and tested.
	const UDataTable* ClassTable = UCataclysmPlayerClassStats::LoadTable();
	if (!ClassTable)
	{
		AddError(TEXT("The class stats table could not be loaded."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCharacter Character(World);
	UAbilitySystemComponent* ASC = Character.AbilitySystem;
	UCataclysmEquipmentComponent* Equipment = Character.Equipment;

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (!ASC->HasAttributeSetForAttribute(MaxHealth))
	{
		AddError(TEXT("The test character holds no vital attribute set."));
		return false;
	}

	Equipment->RefreshAttributes(ASC);
	const float Bare = ASC->GetNumericAttribute(MaxHealth);
	TestTrue(TEXT("a character with no gear has the class line's health"),
		Bare > 0.0f);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(WithHealthAffix(HeadBase), Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(ASC);

	const float Wearing = ASC->GetNumericAttribute(MaxHealth);
	TestTrue(FString::Printf(
		TEXT("wearing a helm with flat maximum health raises it: %.1f to %.1f"),
		Bare, Wearing), Wearing > Bare);

	Equipment->Unequip(ECataclysmGearSlot::Head, Removed);
	Equipment->RefreshAttributes(ASC);

	TestEqual(TEXT("and taking it off puts it back exactly"),
		ASC->GetNumericAttribute(MaxHealth), Bare, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRefreshDoesNotHeal,
	"Cataclysm.Equipment.ChangingGearDoesNotRefillHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRefreshDoesNotHeal::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	// A FREE HEAL IS WHAT THIS GUARDS AGAINST. UCataclysmPlayerClassStats::
	// ApplyTo fills health, mana and shield to their maximums, which is right
	// for a character arriving in the world. Reusing it unchanged here would let
	// a player heal to full mid-fight by taking a ring off and putting it back.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCharacter Character(World);
	UAbilitySystemComponent* ASC = Character.AbilitySystem;
	UCataclysmEquipmentComponent* Equipment = Character.Equipment;

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (!ASC->HasAttributeSetForAttribute(Health))
	{
		AddError(TEXT("The test character holds no vital attribute set."));
		return false;
	}

	Equipment->RefreshAttributes(ASC);

	// Hurt the character to a quarter of its maximum.
	const float Wounded = ASC->GetNumericAttribute(MaxHealth) * 0.25f;
	ASC->SetNumericAttributeBase(Health, Wounded);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(WithHealthAffix(HeadBase), Removed, AlsoRemoved, Slot);
	Equipment->RefreshAttributes(ASC);

	TestEqual(TEXT("putting a helm on does not heal the character"),
		ASC->GetNumericAttribute(Health), Wounded, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// The weapon decides which abilities exist
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEquippedWeaponTypeFollowsTheItem,
	"Cataclysm.Equipment.TheWeaponTypeComesFromTheWornWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEquippedWeaponTypeFollowsTheItem::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	// WEARING NOTHING NAMES NO WEAPON TYPE, and the caller has to be able to
	// tell that from a weapon whose type happens to be blank, because
	// ACataclysmPlayerCharacter falls back to the starting weapon on an empty
	// answer. If this ever answered a type for a character holding nothing, that
	// fallback would stop running and a new character would have no abilities.
	TestTrue(TEXT("a character holding nothing names no weapon type"),
		Equipment->EquippedWeaponType().IsEmpty());

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	Equipment->Equip(Plain(OneHandedBase), Removed, AlsoRemoved, Slot);

	TestEqual(TEXT("a worn sword names the Sword type"),
		Equipment->EquippedWeaponType(), FString(TEXT("Sword")));

	FCataclysmItem TakenOff;
	Equipment->Unequip(ECataclysmGearSlot::Weapon1, TakenOff);
	TestTrue(TEXT("and putting it away names none again"),
		Equipment->EquippedWeaponType().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEquipmentRaisesItsChange,
	"Cataclysm.Equipment.EveryChangeToWhatIsWornIsAnnounced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEquipmentRaisesItsChange::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	// WITHOUT THE ANNOUNCEMENT NOTHING RECOMPUTES. ACataclysmPlayerCharacter
	// binds to this to rewrite the stat line and refill the ability slots, so a
	// change that did not raise it would leave a player wearing a helmet that
	// did nothing until something else happened to trigger a refresh.
	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	int32 Announced = 0;
	Equipment->EquipmentChanged.AddLambda([&Announced] { ++Announced; });

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;

	Equipment->Equip(Plain(HeadBase), Removed, AlsoRemoved, Slot);
	TestEqual(TEXT("putting something on is announced"), Announced, 1);

	Equipment->Unequip(ECataclysmGearSlot::Head, Removed);
	TestEqual(TEXT("taking it off is announced"), Announced, 2);

	// A REFUSAL CHANGES NOTHING AND SO ANNOUNCES NOTHING.
	Equipment->EquipInto(Plain(HeadBase), ECataclysmGearSlot::Boots,
						 Removed, AlsoRemoved);
	TestEqual(TEXT("a refused equip is not announced"), Announced, 2);

	// SO DOES TAKING EVERYTHING OFF A CHARACTER WEARING NOTHING.
	Equipment->UnequipEverything();
	TestEqual(TEXT("stripping a character wearing nothing is not announced"),
		Announced, 2);

	Equipment->Equip(Plain(HeadBase), Removed, AlsoRemoved, Slot);
	Equipment->UnequipEverything();
	TestEqual(TEXT("but stripping one wearing something is"), Announced, 4);

	return true;
}

// ---------------------------------------------------------------------------
// Attack damage and attack speed. Issues #840 and #845
// ---------------------------------------------------------------------------

namespace CataclysmEquipmentTest
{
	/** The flat attack damage prefix. It rolls on Gloves, Necklace, Relic, Ring
	 *  and Weapon, which is what makes it a character stat rather than a weapon
	 *  one. Top value 22 in game/Data/Affixes.csv. */
	const TCHAR* FlatDamageAffix = TEXT("Stat_Flat_damage");

	/** The increased attack speed suffix, top value 15 per cent. */
	const TCHAR* AttackSpeedAffix = TEXT("Stat_Increased_attack_speed");

	/** An item of a named base carrying one perfectly rolled top-tier affix. */
	FCataclysmItem WithAffix(const TCHAR* Base, const TCHAR* Affix,
							 int32 GearLevel = 0)
	{
		FCataclysmItem Item = Plain(Base, GearLevel);

		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(Affix);
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);

		return Item;
	}

	/** Wears each item in turn, then answers one attribute. */
	float AttributeWearing(UWorld* World, const TArray<FCataclysmItem>& Items,
						   const FGameplayAttribute& Attribute)
	{
		FScopedCharacter Character(World);
		for (const FCataclysmItem& Item : Items)
		{
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
			Character.Equipment->Equip(Item, Removed, AlsoRemoved, Went);
		}
		Character.Equipment->RefreshAttributes(Character.AbilitySystem);
		return Character.AbilitySystem->GetNumericAttribute(Attribute);
	}
}

/**
 * A worn weapon's damage reaches the character, and two weapons SUM.
 *
 * WHAT THIS REPLACES. Until issue #845 the attack damage attribute was written
 * by UCataclysmWeaponSlotsComponent from the equipped weapon TYPE. That made a
 * second worn weapon worth nothing and an upgrade level never apply (#840), and
 * it meant two things wrote one attribute. There is one writer now,
 * UCataclysmPlayerClassStats::ApplyTo, and a weapon's damage reaches it as an
 * ordinary flat modifier like any other.
 *
 * THE FIGURES ARE QUOTED FROM THE DESIGN DOCUMENT. docs/Cataclysm_GDD_v2.md
 * line 2627: "an Axe with an Axe at 92 and an Axe with a Sword at 86".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWornWeaponsReachAttackDamage,
	"Cataclysm.Equipment.WornWeaponsReachAttackDamageAndTwoOfThemSum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWornWeaponsReachAttackDamage::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCharacter Character(World);
	const FGameplayAttribute Damage =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();

	const auto AttackDamage = [&]
	{
		Character.Equipment->RefreshAttributes(Character.AbilitySystem);
		return Character.AbilitySystem->GetNumericAttribute(Damage);
	};

	// NOTHING WORN IS WORTH NOTHING, which is what makes the rest meaningful.
	TestEqual(TEXT("a character wearing nothing has no attack damage"),
		AttackDamage(), 0.0f, 0.05f);

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;

	// The sheet states the +10 figure, so a fully upgraded Axe carries 46.
	Character.Equipment->Equip(Plain(TEXT("Weapon_Axe"), 10),
							   Removed, AlsoRemoved, Went);
	TestEqual(TEXT("one worn Axe supplies the 46 the sheet states"),
		AttackDamage(), 46.0f, 0.05f);

	// A SECOND WEAPON ADDS ITS DAMAGE. This is the reported fault: the project
	// owner equipped a second whip and nothing changed at all.
	Character.Equipment->Equip(Plain(OneHandedBase, 10),
							   Removed, AlsoRemoved, Went);
	TestEqual(TEXT("an Axe with a Sword gives 86, the design document's figure"),
		AttackDamage(), 86.0f, 0.05f);

	// TAKING ONE OFF TAKES ITS DAMAGE WITH IT.
	Character.Equipment->Unequip(ECataclysmGearSlot::Weapon2, Removed);
	TestEqual(TEXT("taking the Sword off leaves the Axe's 46"),
		AttackDamage(), 46.0f, 0.05f);

	Character.Equipment->Unequip(ECataclysmGearSlot::Weapon1, Removed);
	TestEqual(TEXT("and taking the last weapon off leaves nothing"),
		AttackDamage(), 0.0f, 0.05f);

	return true;
}

/**
 * A weapon's upgrade level reaches its damage, and a two-hander doubles.
 *
 * WHAT WENT WRONG WITHOUT IT. Issue #840: the upgrade level came from a field on
 * UCataclysmWeaponSlotsComponent that nothing outside the automation tests ever
 * set, so a worn +5 weapon was computed as a +0 one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUpgradeLevelReachesAttackDamage,
	"Cataclysm.Equipment.AWornWeaponsUpgradeLevelReachesAttackDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUpgradeLevelReachesAttackDamage::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayAttribute Damage =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();

	const float Fresh =
		AttributeWearing(World, {Plain(TEXT("Weapon_Whip"), 0)}, Damage);
	const float Upgraded =
		AttributeWearing(World, {Plain(TEXT("Weapon_Whip"), 10)}, Damage);

	TestEqual(TEXT("a +10 Whip carries the 32 the sheet states"),
		Upgraded, 32.0f, 0.05f);
	if (TestTrue(TEXT("a +0 Whip is worth something"), Fresh > 0.0f))
	{
		// About 3.52 times, the upgrade curve stated on FCataclysmItem itself.
		// Asserted as a ratio rather than a second literal, so re-tuning the
		// curve does not break this -- only the upgrade level ceasing to apply,
		// which is the fault.
		TestEqual(TEXT("and a +10 Whip is about 3.52 times a +0 one"),
			Upgraded / Fresh, 3.52f, 0.05f);
	}

	// A TWO-HANDED WEAPON IS WORTH DOUBLE ITS STATED FIGURE, and is one item
	// rather than two, because it is stored in the first weapon slot alone.
	TestEqual(TEXT("a +10 Greatsword's stated 78 doubles to 156"),
		AttributeWearing(World, {Plain(TwoHandedBase, 10)}, Damage),
		156.0f, 0.05f);

	return true;
}

/**
 * AN ATTACK DAMAGE AFFIX REACHES THE CHARACTER, WHEREVER IT SITS. Issue #845.
 *
 * THIS IS THE FAULT THAT ISSUE WAS ABOUT. Flat attack damage, increased attack
 * damage and increased attack speed all roll on Gloves, Necklace, Relic, Ring
 * and Weapon -- five slot families -- and none of them did anything at all.
 * UCataclysmEquipmentComponent::GatherModifiers gathered them and
 * UCataclysmPlayerClassStats::ApplyTo dropped them, because its loop is over the
 * attribute map and `attack_damage` was not in it.
 *
 * A RING IS THE CASE THAT PROVES IT. A test using only a weapon would still pass
 * if the affix were being read as part of the weapon rather than as a stat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageAffixesReachTheCharacter,
	"Cataclysm.Equipment.AnAttackDamageAffixReachesTheCharacterFromAnySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDamageAffixesReachTheCharacter::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayAttribute Damage =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();

	const FCataclysmItem Axe = Plain(TEXT("Weapon_Axe"), 10);
	const float WeaponAlone = AttributeWearing(World, {Axe}, Damage);

	// ON A RING, WHICH IS NOT A WEAPON AT ALL.
	const float WithRing = AttributeWearing(
		World, {Axe, WithAffix(RingBase, FlatDamageAffix, 10)}, Damage);
	TestTrue(FString::Printf(
		TEXT("a flat damage affix on a RING raises attack damage, %.2f to %.2f"),
		WeaponAlone, WithRing),
		WithRing > WeaponAlone + 0.05f);

	// ON THE WEAPON ITSELF, which must also work and did not either.
	const float WithWeaponAffix = AttributeWearing(
		World, {WithAffix(TEXT("Weapon_Axe"), FlatDamageAffix, 10)}, Damage);
	TestTrue(FString::Printf(
		TEXT("a flat damage affix on the WEAPON raises it too, %.2f to %.2f"),
		WeaponAlone, WithWeaponAffix),
		WithWeaponAffix > WeaponAlone + 0.05f);

	// THE SAME AFFIX IS WORTH DOUBLE ON A TWO-HANDED WEAPON, which is the rule
	// that makes one two-hander and two one-handers come out equal in affix
	// value. Each weapon's own damage is taken off first, so only the affix is
	// being measured.
	const float TwoHandedBare =
		AttributeWearing(World, {Plain(TwoHandedBase, 10)}, Damage);
	const float TwoHandedWithAffix = AttributeWearing(
		World, {WithAffix(TwoHandedBase, FlatDamageAffix, 10)}, Damage);

	const float OnOneHand = WithWeaponAffix - WeaponAlone;
	const float OnTwoHand = TwoHandedWithAffix - TwoHandedBare;
	if (TestTrue(TEXT("the affix is worth something on a one-handed weapon"),
			OnOneHand > 0.05f))
	{
		TestEqual(TEXT("and exactly twice that on a two-handed one"),
			OnTwoHand, OnOneHand * 2.0f, 0.05f);
	}

	return true;
}

/**
 * A character's swing rate is the average of its weapons, and affixes raise it.
 *
 * TWO SEPARATE FAULTS, BOTH FROM ISSUE #845. A weapon's own attack speed
 * implicit did nothing -- a Sword states `attack_speed increased 5` in
 * game/Data/ItemBases.csv and never got it -- and neither did an increased
 * attack speed affix on any piece of gear.
 *
 * THE RATE IS A SUPPLIED BASE, NOT A MODIFIER, and that is why it needs its own
 * test. A rate is neither an implicit nor an affix, and two weapons AVERAGE
 * their rates rather than summing them, so
 * UCataclysmEquipmentComponent::StatBasesFromWeapons hands it to
 * UCataclysmPlayerClassStats::ApplyTo separately from every other stat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttackSpeedReachesTheCharacter,
	"Cataclysm.Equipment.AttackSpeedAveragesAcrossWeaponsAndAffixesRaiseIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttackSpeedReachesTheCharacter::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayAttribute Speed =
		UCataclysmCombatAttributeSet::GetAttackSpeedAttribute();

	// An Axe states 1.25 and carries no attack speed implicit of its own.
	TestEqual(TEXT("one Axe swings at its own stated 1.25"),
		AttributeWearing(World, {Plain(TEXT("Weapon_Axe"))}, Speed),
		1.25f, 0.005f);

	// A SWORD'S OWN IMPLICIT APPLIES, and it never did before. The Sword states
	// 1.3 and carries `attack_speed increased 5`, so 1.3 x 1.05 = 1.365.
	//
	// AT +10, AND THE UPGRADE LEVEL IS NOT INCIDENTAL HERE. Every stated figure
	// in the sheets is the +10 one, implicits included, so a +0 Sword's implicit
	// is worth about 1.42 per cent rather than 5 and the rate comes out 1.318.
	// That is correct behaviour and it is easy to read as a broken test.
	TestEqual(TEXT("a Sword's own 5 per cent attack speed implicit applies"),
		AttributeWearing(World, {Plain(OneHandedBase, 10)}, Speed),
		1.365f, 0.005f);

	// TWO WEAPONS AVERAGE THEIR RATES rather than summing them, which is what
	// stops summing their damage being a strict advantage. The mean of 1.25 and
	// 1.3 is 1.275, and the Sword's own 5 per cent then applies to the pair.
	TestEqual(TEXT("an Axe with a Sword averages to 1.275, then the implicit"),
		AttributeWearing(World,
			{Plain(TEXT("Weapon_Axe"), 10), Plain(OneHandedBase, 10)}, Speed),
		1.275f * 1.05f, 0.005f);

	// A SHIELD IS NOT SWUNG, so it is left out of the average entirely. An
	// average over everything worn would answer 1.225 here rather than 1.25.
	TestEqual(TEXT("an Axe with a Shield still swings at the Axe's 1.25"),
		AttributeWearing(World,
			{Plain(TEXT("Weapon_Axe")), Plain(TEXT("Weapon_Shield"))}, Speed),
		1.25f, 0.005f);

	// AN AFFIX ON A PIECE THAT IS NOT A WEAPON RAISES IT, which is the half a
	// test using only weapons would miss.
	const float Bare =
		AttributeWearing(World, {Plain(TEXT("Weapon_Axe"))}, Speed);
	const float WithRing = AttributeWearing(World,
		{Plain(TEXT("Weapon_Axe")), WithAffix(RingBase, AttackSpeedAffix, 10)},
		Speed);
	TestTrue(FString::Printf(
		TEXT("an attack speed affix on a RING raises the rate, %.3f to %.3f"),
		Bare, WithRing),
		WithRing > Bare + 0.005f);

	// NOTHING ARMED SWINGS AT NOTHING rather than keeping the last weapon's
	// rate. Zero is read by the automatic basic attack as never swinging.
	TestEqual(TEXT("a character wearing no weapon has no swing rate"),
		AttributeWearing(World, {}, Speed), 0.0f, 0.005f);

	// SWAPPING REPLACES RATHER THAN ACCUMULATING, which is issue #647's rule.
	// It used to be checked in Cataclysm.BasicAttack against the weapon slots
	// component, which no longer writes any attribute, so it is checked here
	// instead. A Dagger states 1.5 and a Greatsword 1.25; a character who ended
	// up with 2.75, or with the faster of the two, would be accumulating.
	{
		FScopedCharacter Character(World);
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;

		const auto Rate = [&]
		{
			Character.Equipment->RefreshAttributes(Character.AbilitySystem);
			return Character.AbilitySystem->GetNumericAttribute(Speed);
		};

		Character.Equipment->Equip(Plain(OtherOneHandedBase),
								   Removed, AlsoRemoved, Went);
		TestEqual(TEXT("a Dagger swings at its stated 1.5"), Rate(), 1.5f, 0.005f);

		// A two-handed weapon takes the Dagger off as it goes on.
		Character.Equipment->Equip(Plain(TwoHandedBase),
								   Removed, AlsoRemoved, Went);
		TestEqual(TEXT("and a Greatsword over it swings at 1.25, not 2.75"),
			Rate(), 1.25f, 0.005f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Whether every affix in the data reaches the character at all
// ---------------------------------------------------------------------------

/**
 * WHAT THESE TWO TESTS ADD, AND WHERE THE EXISTING GUARD STOPS.
 *
 * On 2026-08-23 the project owner said they did not believe the gear affixes
 * were working. They were right: twenty-five of the eighty-five rows of
 * game/Data/Affixes.csv changed something a player could feel.
 *
 * ISSUE #894 RAISED THAT TO FORTY, by giving twelve stat names an attribute to
 * be written to, and ISSUE #896 RAISED IT TO FORTY-THREE by giving magic find a
 * thirteenth and making a kill read what the player is carrying. Of the
 * seventy-four rows that grant a stat, fifty-one now have an attribute behind
 * every stat they grant and twenty-three have none. Eight of the fifty-one then
 * reach an attribute nothing reads (#895), or one that only clamps a pool
 * nothing fills or spends, which is the class resource and is issue #192.
 *
 * The rest are broken in four ways, filed as #895, #897, #898 and #899.
 *
 * Cataclysm.Items.EveryAffixInTheDataGrantsSomething already asserts that every
 * affix produces a MODIFIER, and every one of them does. The chain has two more
 * links after that and it had nothing guarding either:
 *
 *   **The modifier has to reach a gameplay attribute.**
 *   UCataclysmPlayerClassStats::ApplyTo loops over StatToAttribute rather than
 *   over the modifiers it is handed, so a modifier naming a stat that map does
 *   not hold is dropped without a warning. That is what issue #845 was, for two
 *   stat names; thirty-five more are still in that state.
 *
 *   **Some arithmetic has to read the attribute.** An attribute can be written
 *   correctly and read by nothing, which is what issue #481 was on the enemy
 *   side, where damage needed to kill understated by up to 56%.
 *
 * THESE TESTS COVER THE FIRST OF THOSE TWO AND NOT THE SECOND, and that is
 * stated here rather than left to be assumed. A test that some arithmetic
 * somewhere reads an attribute is not something the automation framework can
 * express, so #895 has to be checked by reading the source. That is exactly why
 * #481 survived as long as it did.
 */
namespace CataclysmEquipmentTest
{
	/**
	 * The stat names an affix grants that ApplyTo writes no attribute from.
	 *
	 * EVERY ENTRY IS A KNOWN FAULT WITH AN ISSUE, not a stat that is meant to
	 * be absent. The list is exact in both directions: a stat missing an
	 * attribute that is not named here fails the test, and a stat named here
	 * that now has one fails it too, so the list has to be shortened as the
	 * work lands rather than left standing.
	 */
	const TSet<FString>& StatsNoAttributeIsWrittenFrom()
	{
		static const TSet<FString> Stats = {
			// #894 DELETED TWELVE NAMES FROM HERE -- evasion, block chance,
			// critical strike chance, penetration and the eight resistances --
			// by giving each a StatToAttribute entry. The arithmetic that reads
			// them already existed, which is what made that issue the cheap one.
			//
			// #895. The attribute exists and NOTHING READS IT, so a map entry
			// on its own would leave these eleven doing just as little.
			TEXT("cooldown_reduction"),
			TEXT("mana_leech"), TEXT("energy_shield_leech"),
			TEXT("damage_vs_war"), TEXT("damage_vs_demonic"),
			TEXT("damage_vs_death"), TEXT("damage_vs_pestilence"),
			TEXT("damage_vs_famine"), TEXT("damage_vs_celestial"),
			TEXT("damage_vs_chaos"), TEXT("damage_vs_void"),

			// #897. No primary attribute is written or read anywhere in the
			// game, so these eight have nowhere to go and nothing to do.
			TEXT("agility"), TEXT("ferocity"), TEXT("constitution"),
			TEXT("vitality"), TEXT("mind"), TEXT("spirit"),
			TEXT("efficacy"), TEXT("luck"),

			// #898. No gameplay attribute for any minion stat exists at all,
			// so unlike everything above there is nothing to map these to.
			TEXT("minion_damage"), TEXT("minion_health"),
			TEXT("minion_attack_speed"),
		};
		return Stats;
	}

	/** An item of a base, carrying one perfectly rolled top-tier affix. */
	FCataclysmItem Carrying(const TCHAR* Base, const FName& AffixKey,
							int32 Breadth)
	{
		FCataclysmItem Item = Plain(Base, UCataclysmItemValues::MaxGearLevel);

		FCataclysmRolledAffix Rolled;
		Rolled.Affix = AffixKey;
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;

		// A resistance family states how many damage types it covers and the
		// item states which, so it has to be handed as many as it expects or
		// AccumulateInto refuses the affix outright and grants nothing -- which
		// would look exactly like the fault being tested for.
		const TArray<FName>& AllTypes = UCataclysmItemModifiers::DamageTypeNames();
		for (int32 Index = 0; Index < Breadth && Index < AllTypes.Num(); ++Index)
		{
			Rolled.DamageTypes.Add(AllTypes[Index]);
		}

		Item.Affixes.Add(Rolled);
		return Item;
	}

	/** The stat names an affix adds, over what the bare base grants by itself. */
	TSet<FString> StatsGrantedBy(const TCHAR* Base, const FName& AffixKey,
								 int32 Breadth, const UDataTable* Bases,
								 const UDataTable* Affixes)
	{
		TMap<FName, int32> FromTheBase;
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Pair :
				UCataclysmItemModifiers::ModifiersFor(
					Plain(Base, UCataclysmItemValues::MaxGearLevel),
					Bases, Affixes))
		{
			FromTheBase.Add(Pair.Key, Pair.Value.Num());
		}

		TSet<FString> Granted;
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Pair :
				UCataclysmItemModifiers::ModifiersFor(
					Carrying(Base, AffixKey, Breadth), Bases, Affixes))
		{
			// COUNTED RATHER THAN LOOKED FOR. A Head_Helm carries a flat armour
			// implicit of 200 of its own, so an affix granting armour would be
			// invisible to a check that only asked whether the stat is present.
			if (Pair.Value.Num() > FromTheBase.FindRef(Pair.Key))
			{
				Granted.Add(Pair.Key.ToString());
			}
		}
		return Granted;
	}

	/** For each stat, an affix granting it flat, so a base can be supplied. */
	TMap<FString, FName> FlatAffixPerStat(const UDataTable* Affixes)
	{
		TMap<FString, FName> Found;
		Affixes->ForeachRow<FCataclysmAffixRow>(
			TEXT("FlatAffixPerStat"),
			[&Found](const FName& Key, const FCataclysmAffixRow& Row)
			{
				if (Row.ValueKind.Equals(TEXT("flat"), ESearchCase::IgnoreCase)
					&& !Row.Stat.IsEmpty() && !Found.Contains(Row.Stat))
				{
					Found.Add(Row.Stat, Key);
				}
			});
		return Found;
	}

	/** Every attribute on every set a player carries. */
	const TArray<FGameplayAttribute>& EveryPlayerAttribute()
	{
		// BUILT ON FIRST USE RATHER THAN AS A FILE-SCOPE STATIC, for the same
		// reason UCataclysmPlayerClassStats::StatToAttribute is: an
		// FGameplayAttribute wraps an FProperty found by reflection, and that
		// reflection data is not ready during static initialisation.
		static const TArray<FGameplayAttribute> All = []
		{
			TArray<FGameplayAttribute> Out;
			Out.Append(UCataclysmVitalAttributeSet::GetAllAttributes());
			Out.Append(UCataclysmPrimaryAttributeSet::GetAllAttributes());
			Out.Append(UCataclysmCombatAttributeSet::GetAllAttributes());
			Out.Append(UCataclysmResistanceAttributeSet::GetAllAttributes());
			Out.Append(UCataclysmClassResourceAttributeSet::GetAllAttributes());
			return Out;
		}();
		return All;
	}

	/**
	 * What a character wearing these ends up with, every attribute at once.
	 *
	 * THIS IS WHAT UCataclysmEquipmentComponent::RefreshAttributes DOES, with
	 * the class named outright instead of read from the Cataclysm.PlayerClass
	 * console variable. Both gather the modifiers and the weapon bases from the
	 * equipment component and hand them to ApplyTo.
	 *
	 * THE RITUALIST, AND THE CHOICE MATTERS. An increased modifier multiplies a
	 * base, so on a base of zero it moves nothing however well the rest of the
	 * chain works. The Ritualist is the only line of game/Data/ClassStats.csv
	 * stating a spell damage, maximum energy shield and energy shield
	 * regeneration base, and every other mapped stat is either stated by the
	 * shared Default line, supplied by the worn weapon, or given a flat affix to
	 * stand on by the caller below.
	 */
	TArray<float> AttributesWearing(UWorld* World,
									const TArray<FCataclysmItem>& Items)
	{
		FScopedCharacter Character(World);
		for (const FCataclysmItem& Item : Items)
		{
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
			Character.Equipment->Equip(Item, Removed, AlsoRemoved, Went);
		}

		const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
			Character.Equipment->GatherModifiers();
		const TMap<FName, float> Bases =
			Character.Equipment->StatBasesFromWeapons();

		UCataclysmPlayerClassStats::ApplyTo(
			Character.AbilitySystem, UCataclysmPlayerClassStats::LoadTable(),
			TEXT("Ritualist"), UCataclysmPlayerClassStats::DefaultLevel,
			&Modifiers, ECataclysmPoolFill::LeaveAsTheyAre, &Bases);

		TArray<float> Values;
		for (const FGameplayAttribute& Attribute : EveryPlayerAttribute())
		{
			Values.Add(Character.AbilitySystem->GetNumericAttribute(Attribute));
		}
		return Values;
	}

	/** The first attribute that differs and by how much, or empty if none does. */
	FString FirstAttributeThatMoved(const TArray<float>& Before,
									const TArray<float>& After)
	{
		const TArray<FGameplayAttribute>& All = EveryPlayerAttribute();
		for (int32 Index = 0;
			 Index < All.Num() && Index < Before.Num() && Index < After.Num();
			 ++Index)
		{
			if (!FMath::IsNearlyEqual(Before[Index], After[Index], 0.0001f))
			{
				return FString::Printf(TEXT("%s went from %.4f to %.4f"),
					*All[Index].GetName(), Before[Index], After[Index]);
			}
		}
		return FString();
	}
}

/**
 * EVERY STAT AN AFFIX GRANTS HAS AN ATTRIBUTE BEHIND IT. Issues #894 to #898.
 *
 * THIS IS THE GUARD THAT WOULD HAVE CAUGHT #845 AND CATCHES WHAT IS LEFT OF IT.
 * It reads the stat names out of the affix data through the real accumulation
 * code rather than listing them here, so an affix the design adds is checked the
 * day it arrives.
 *
 * IT CATCHES A HALF-DEAD HYBRID, which the end-to-end test below cannot. Six
 * hybrid affixes grant one stat that lands and one that does not, so the
 * character does change and only half of what the tool tip promised arrives.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryAffixStatHasAnAttribute,
	"Cataclysm.Equipment.EveryStatAnAffixGrantsHasAnAttributeBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryAffixStatHasAnAttribute::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	if (!Bases || !Affixes)
	{
		AddError(TEXT("The item base or affix table could not be loaded."));
		return false;
	}

	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();

	TSet<FString> Missing;
	int32 Checked = 0;
	int32 WholeAffixReaches = 0;

	Affixes->ForeachRow<FCataclysmAffixRow>(
		TEXT("EveryStatAnAffixGrantsHasAnAttributeBehindIt"),
		[&](const FName& Key, const FCataclysmAffixRow& Row)
		{
			// AN AILMENT AFFIX GRANTS NO STAT AND THAT IS CORRECT. Whether
			// anything ever applies the ailment is issue #899 and a different
			// question from this one.
			if (Row.AffixKind.Equals(TEXT("Ailment"), ESearchCase::IgnoreCase))
			{
				return;
			}

			++Checked;
			bool bWholeAffixReaches = true;

			for (const FString& Stat :
					StatsGrantedBy(HeadBase, Key, Row.Breadth, Bases, Affixes))
			{
				if (Map.Contains(Stat))
				{
					continue;
				}

				bWholeAffixReaches = false;
				Missing.Add(Stat);

				if (!StatsNoAttributeIsWrittenFrom().Contains(Stat))
				{
					AddError(FString::Printf(
						TEXT("The affix %s grants '%s', and "
							 "UCataclysmPlayerClassStats::StatToAttribute has no "
							 "entry for it, so ApplyTo never writes it and the "
							 "affix does nothing at all. Give the stat an "
							 "attribute, or add it to "
							 "StatsNoAttributeIsWrittenFrom naming the issue it "
							 "is filed under."),
						*Key.ToString(), *Stat));
				}
			}

			if (bWholeAffixReaches)
			{
				++WholeAffixReaches;
			}
		});

	AddInfo(FString::Printf(
		TEXT("%d of %d affixes that grant a stat have an attribute behind every "
			 "stat they grant. %d stat names have none."),
		WholeAffixReaches, Checked, Missing.Num()));

	// Without this the loop above passes on an empty table, which is what a
	// stale or unbuilt asset actually looks like.
	TestTrue(FString::Printf(TEXT("most of the affix pool was checked, %d of it"),
							 Checked),
		Checked >= 60);

	// AND THE LIST HAS TO SHRINK AS THE WORK LANDS. A stat named there that now
	// reaches an attribute is an exemption left behind, and leaving one standing
	// would let the same stat break again with nothing noticing.
	for (const FString& Stat : StatsNoAttributeIsWrittenFrom())
	{
		if (!Missing.Contains(Stat))
		{
			AddError(FString::Printf(
				TEXT("'%s' is listed in StatsNoAttributeIsWrittenFrom as having "
					 "no attribute behind it, and it now has one, or no affix "
					 "grants it any more. Delete it from that list."), *Stat));
		}
	}

	return true;
}

/**
 * EVERY AFFIX THAT CAN REACH A WORN CHARACTER DOES. Issues #894 to #898.
 *
 * THE END-TO-END MEASURE, and the one the check above cannot be. It equips a
 * real item on a real character with all five attribute sets, gathers the
 * modifiers through UCataclysmEquipmentComponent and writes them through
 * UCataclysmPlayerClassStats::ApplyTo, which is the path a player takes, then
 * compares every attribute before and after.
 *
 * MEASURED ON A CHARACTER THAT HAS SOMETHING, NOT ON A BARE ONE. An increased
 * modifier multiplies a base, so measuring one on a character whose base is zero
 * answers a question about the class table rather than about the affix. A weapon
 * is worn for the two stats a weapon supplies, and where the data has a flat
 * affix for a stat the affix touches, one is worn on another slot in BOTH
 * measurements. Pull request #892 exists because a distribution was measured
 * without what a player actually carries.
 *
 * IT ASSERTS IN BOTH DIRECTIONS. An affix every one of whose stats is listed in
 * StatsNoAttributeIsWrittenFrom must move nothing at all; if it moves something,
 * that list is stale and says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryAffixReachesTheCharacter,
	"Cataclysm.Equipment.EveryAffixInTheDataReachesTheCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryAffixReachesTheCharacter::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	if (!Bases || !Affixes)
	{
		AddError(TEXT("The item base or affix table could not be loaded."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();
	const TMap<FString, FName> FlatFor = FlatAffixPerStat(Affixes);

	// Two is enough: a hybrid is the widest affix that reaches the map and it
	// grants two stats. A resistance family grants eight and none of them
	// reaches the map, so it needs none of these.
	const TCHAR* CompanionBases[] = { BootsBase, BeltBase };

	// GATHERED FIRST, because the body below spawns and destroys actors and
	// that is not something to do while a data table is being iterated.
	struct FAffixUnderTest
	{
		FName Key;
		int32 Breadth = 0;
		bool bIsAilment = false;
	};

	TArray<FAffixUnderTest> Pool;
	Affixes->ForeachRow<FCataclysmAffixRow>(
		TEXT("EveryAffixInTheDataReachesTheCharacter"),
		[&Pool](const FName& Key, const FCataclysmAffixRow& Row)
		{
			FAffixUnderTest Entry;
			Entry.Key = Key;
			Entry.Breadth = Row.Breadth;
			Entry.bIsAilment =
				Row.AffixKind.Equals(TEXT("Ailment"), ESearchCase::IgnoreCase);
			Pool.Add(Entry);
		});

	int32 Reached = 0;
	int32 ReachedNothing = 0;
	TArray<FString> Dead;

	for (const FAffixUnderTest& Entry : Pool)
	{
		if (Entry.bIsAilment)
		{
			continue;
		}

		const TSet<FString> Granted =
			StatsGrantedBy(HeadBase, Entry.Key, Entry.Breadth, Bases, Affixes);

		TArray<FCataclysmItem> Worn;
		Worn.Add(Plain(OneHandedBase, UCataclysmItemValues::MaxGearLevel));

		int32 NextCompanion = 0;
		bool bAnyStatReachesAnAttribute = false;
		for (const FString& Stat : Granted)
		{
			if (!Map.Contains(Stat))
			{
				continue;
			}
			bAnyStatReachesAnAttribute = true;

			if (NextCompanion >= UE_ARRAY_COUNT(CompanionBases))
			{
				continue;
			}
			if (const FName* FlatAffix = FlatFor.Find(Stat))
			{
				Worn.Add(Carrying(CompanionBases[NextCompanion], *FlatAffix,
								  /*Breadth=*/0));
				++NextCompanion;
			}
		}

		TArray<FCataclysmItem> Without = Worn;
		Without.Add(Plain(HeadBase, UCataclysmItemValues::MaxGearLevel));

		TArray<FCataclysmItem> With = Worn;
		With.Add(Carrying(HeadBase, Entry.Key, Entry.Breadth));

		// THE HEAD SLOT AND NOT A RING, because there are eight ring slots and a
		// second ring would go beside the first rather than replacing it, so
		// both measurements would be of a character wearing both.
		const FString Difference = FirstAttributeThatMoved(
			AttributesWearing(World, Without), AttributesWearing(World, With));

		if (bAnyStatReachesAnAttribute)
		{
			++Reached;
			TestTrue(FString::Printf(
				TEXT("wearing %s changes an attribute on the character: %s"),
				*Entry.Key.ToString(),
				Difference.IsEmpty() ? TEXT("it changed nothing") : *Difference),
				!Difference.IsEmpty());
		}
		else
		{
			++ReachedNothing;
			Dead.Add(Entry.Key.ToString());
			TestTrue(FString::Printf(
				TEXT("%s grants only stats listed in "
					 "StatsNoAttributeIsWrittenFrom, so wearing it changes "
					 "nothing. It changed %s, so that list is stale."),
				*Entry.Key.ToString(), *Difference),
				Difference.IsEmpty());
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d affixes reach an attribute on a worn character. %d reach none "
			 "at all: %s. Reaching an attribute is not the same as doing "
			 "something -- see #895 for the ones nothing reads."),
		Reached, ReachedNothing, *FString::Join(Dead, TEXT(", "))));

	// Without this the loop above passes having worn nothing, which is what a
	// stale or unbuilt asset actually looks like.
	TestTrue(FString::Printf(TEXT("most of the affix pool was worn, %d of it"),
							 Reached + ReachedNothing),
		Reached + ReachedNothing >= 60);

	return true;
}


/**
 * A CRITICAL STRIKE CHANCE AFFIX SCALES THE SKILL'S BASE. Issue #894.
 *
 * THE DESIGN DECIDES WHOSE NUMBER THIS IS, and it is not the character's:
 * "Critical strike chance belongs to the skill, not the character. Each skill
 * carries its own base chance, and the character's gear and attributes scale
 * it." So the base has to arrive from what is held and the affixes have to
 * multiply it, and before this issue neither happened.
 *
 * TWO WRITERS WERE THE FAULT, NOT ONE MISSING ONE.
 * UCataclysmWeaponSlotsComponent::ApplyBaseCritChance SET the attribute on every
 * equip, so even after `crit_chance` gained a StatToAttribute entry an affix
 * could not have survived the next weapon change. That function is gone and
 * UCataclysmPlayerClassStats::ApplyTo is the only writer, which is the same
 * resolution issue #845 reached for attack damage.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritChanceAffixesScaleTheSkillBase,
	"Cataclysm.Equipment.ACriticalStrikeChanceAffixScalesTheSkillsBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCritChanceAffixesScaleTheSkillBase::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	if (!Affixes)
	{
		AddError(TEXT("The affix table could not be loaded."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayAttribute Crit =
		UCataclysmCombatAttributeSet::GetCritChanceAttribute();
	const float Base =
		UCataclysmWeaponSlotsComponent::DefaultSkillCritChancePercent;

	// FIVE RATHER THAN WHATEVER THE CONSTANT SAYS. Comparing the attribute
	// against the constant alone would pass if both were zero, which is the
	// state this whole test exists to catch.
	TestEqual(TEXT("the skill default is five percent"), Base, 5.0f, 0.001f);

	const FCataclysmItem Sword = Plain(OneHandedBase,
									   UCataclysmItemValues::MaxGearLevel);

	TestEqual(TEXT("a character holding a weapon has the skill's base chance"),
		AttributeWearing(World, {Sword}, Crit), Base, 0.001f);

	// A CHARACTER HOLDING NOTHING HAS NONE, which is the design's "a character
	// has no critical strike chance in the abstract". Taking the last weapon off
	// is refused in play (issue #841), so this is a state a player cannot reach,
	// but the attribute must not keep what the last weapon gave it.
	TestEqual(TEXT("and a character holding nothing has none"),
		AttributeWearing(World, {}, Crit), 0.0f, 0.001f);

	// A FLAT AFFIX ADDS TO THE BASE.
	const FName FlatAffix = FName(TEXT("Stat_Flat_critical_strike_chance"));
	const float WithFlat = AttributeWearing(
		World, {Sword, Carrying(RingBase, FlatAffix, /*Breadth=*/0)}, Crit);
	TestTrue(FString::Printf(
		TEXT("a flat critical strike chance affix raises it, %.3f to %.3f"),
		Base, WithFlat),
		WithFlat > Base + 0.001f);

	// AN INCREASED AFFIX MULTIPLIES IT, AND THAT IS THE WHOLE POINT. Adding
	// would give the same answer as the flat affix and would say nothing about
	// whether the three-bucket pipeline is being used.
	//
	// THE EXPECTED FIGURE IS READ OFF THE AFFIX ROW rather than written here, so
	// re-tuning the affix does not break this test -- only the increase ceasing
	// to apply, which is the fault.
	const FName IncreasedAffix =
		FName(TEXT("Stat_Increased_critical_strike_chance"));
	const FCataclysmAffixRow* Row = Affixes->FindRow<FCataclysmAffixRow>(
		IncreasedAffix, TEXT("test"), /*bWarnIfMissing=*/false);
	if (!TestNotNull(TEXT("the increased critical strike chance affix exists"),
					 Row))
	{
		return false;
	}

	const float Increase = UCataclysmItemValues::AffixValue(
		Row->TopValue, UCataclysmItemValues::MaxAffixTier, /*Roll=*/1.0f,
		UCataclysmItemValues::MaxGearLevel, /*bTwoHanded=*/false);

	const float WithIncreased = AttributeWearing(
		World, {Sword, Carrying(RingBase, IncreasedAffix, /*Breadth=*/0)}, Crit);

	TestTrue(TEXT("the increased affix is worth something at all"),
		Increase > 0.0f);
	TestEqual(TEXT("an increased affix multiplies the base, not adds to it"),
		WithIncreased, Base * (1.0f + Increase / 100.0f), 0.01f);

	return true;
}

/**
 * EVASION, BLOCK, PENETRATION AND RESISTANCE REACH THE CHARACTER. Issue #894.
 *
 * ALL RESISTANCE GEAR DID NOTHING AT ALL. The three resistance families in
 * game/Data/Affixes.csv are the only source of resistance in the game and no
 * class line names one, so every hit a player took was resolved against a
 * resistance of zero however much resistance gear they wore.
 *
 * THE ARITHMETIC WAS ALREADY THERE FOR ALL FOUR, which is what separated this
 * issue from #895. UCataclysmDamageCalculation rolls evasion at line 278, rolls
 * a block at line 323 and subtracts resistance at line 58, and
 * UCataclysmVitalAttributeSet reads penetration where the hit resolves. Only the
 * StatToAttribute entry was missing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDefensiveAffixesReachTheCharacter,
	"Cataclysm.Equipment.EvasionBlockPenetrationAndResistanceAffixesReachTheCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDefensiveAffixesReachTheCharacter::RunTest(const FString& Parameters)
{
	using namespace CataclysmEquipmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a character in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	struct FCase
	{
		const TCHAR* Affix;
		FGameplayAttribute Attribute;
		const TCHAR* Named;
	};

	const FCase Cases[] = {
		{TEXT("Stat_Flat_evasion"),
		 UCataclysmCombatAttributeSet::GetEvasionAttribute(), TEXT("evasion")},
		{TEXT("Stat_Flat_block_chance"),
		 UCataclysmCombatAttributeSet::GetBlockChanceAttribute(),
		 TEXT("block chance")},
		{TEXT("Stat_Flat_penetration"),
		 UCataclysmCombatAttributeSet::GetPenetrationAttribute(),
		 TEXT("penetration")},
	};

	for (const FCase& Case : Cases)
	{
		// NO CLASS LINE NAMES ANY OF THE THREE, so a bare character has zero and
		// anything above zero came from the affix. Asserting the bare figure as
		// well is what stops this passing on a character that already had some.
		TestEqual(FString::Printf(
			TEXT("a character wearing nothing has no %s"), Case.Named),
			AttributeWearing(World, {}, Case.Attribute), 0.0f, 0.001f);

		const float Worn = AttributeWearing(
			World, {Carrying(HeadBase, FName(Case.Affix), /*Breadth=*/0)},
			Case.Attribute);

		TestTrue(FString::Printf(
			TEXT("wearing %s grants %s, which came out at %.3f"),
			Case.Affix, Case.Named, Worn),
			Worn > 0.001f);
	}

	// A SINGLE RESISTANCE AFFIX COVERS ONE DAMAGE TYPE AND NOT THE OTHER SEVEN.
	// Carrying fills the item's damage type list from the front of
	// DamageTypeNames, so a breadth of one is War.
	const FCataclysmItem OneResistance = Carrying(
		HeadBase, FName(TEXT("Resistance_Single_resistance")), /*Breadth=*/1);

	const float War = AttributeWearing(World, {OneResistance},
		UCataclysmResistanceAttributeSet::GetWarResistanceAttribute());
	const float Demonic = AttributeWearing(World, {OneResistance},
		UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute());

	TestTrue(FString::Printf(
		TEXT("a single resistance affix naming War grants War resistance, %.3f"),
		War), War > 0.001f);
	TestEqual(TEXT("and grants no resistance to a type it does not name"),
		Demonic, 0.0f, 0.001f);

	// AND THE ALL-RESISTANCES FAMILY REACHES EVERY ONE OF THE EIGHT. A loop over
	// the damage type names rather than eight lines, so a ninth damage type
	// would be checked without anybody editing this.
	const FCataclysmItem AllResistances = Carrying(
		HeadBase, FName(TEXT("Resistance_All_resistances")),
		UCataclysmItemModifiers::DamageTypeNames().Num());

	const TMap<FName, FGameplayAttribute> ResistanceOf = {
		{TEXT("War"), UCataclysmResistanceAttributeSet::GetWarResistanceAttribute()},
		{TEXT("Demonic"), UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute()},
		{TEXT("Death"), UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute()},
		{TEXT("Pestilence"), UCataclysmResistanceAttributeSet::GetPestilenceResistanceAttribute()},
		{TEXT("Famine"), UCataclysmResistanceAttributeSet::GetFamineResistanceAttribute()},
		{TEXT("Celestial"), UCataclysmResistanceAttributeSet::GetCelestialResistanceAttribute()},
		{TEXT("Chaos"), UCataclysmResistanceAttributeSet::GetChaosResistanceAttribute()},
		{TEXT("Void"), UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute()},
	};

	for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
	{
		const FGameplayAttribute* Attribute = ResistanceOf.Find(Type);
		if (!TestNotNull(*FString::Printf(
				TEXT("%s is a damage type this test knows an attribute for"),
				*Type.ToString()), Attribute))
		{
			continue;
		}

		TestTrue(FString::Printf(
			TEXT("the all-resistances affix grants %s resistance"),
			*Type.ToString()),
			AttributeWearing(World, {AllResistances}, *Attribute) > 0.001f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
