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

	for (const TCHAR* Base : {HeadBase, RingBase, BootsBase, OneHandedBase,
							  OtherOneHandedBase, TwoHandedBase})
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

#endif // WITH_AUTOMATION_TESTS
