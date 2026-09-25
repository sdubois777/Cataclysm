// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmGearPanel.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Both Hands Full, `Ravager_capstone_200` option 2: "You may hold a two-handed
 * weapon in each hand. Both contribute their damage, their affixes and their
 * sockets." Issue #1515.
 *
 * THE STAT IS GIVEN BY HAND, as its row will give it: the option has no row
 * yet, so none of these can see a missing or wrong one. The rows change has to
 * add a test that wears the real row.
 */
namespace CataclysmBothHandsFullTest
{
	const TCHAR* Greatsword = TEXT("Weapon_Greatsword");
	const TCHAR* Greataxe = TEXT("Weapon_Greataxe");
	const TCHAR* Sword = TEXT("Weapon_Sword");
	const TCHAR* Dagger = TEXT("Weapon_Dagger");
	const TCHAR* Boots = TEXT("Boots_Sabatons");

	/** A real affix granting flat maximum health, top value 120. */
	const TCHAR* HealthAffix = TEXT("Stat_Flat_maximum_health");

	FCataclysmItem Of(const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		return Item;
	}

	FCataclysmItem WithHealthAffix(const TCHAR* Base)
	{
		FCataclysmItem Item = Of(Base);
		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(HealthAffix);
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);
		return Item;
	}

	/** The option given by hand as a flag of 1, or taken away. */
	void Hold(UCataclysmAbilitySystemComponent* System, bool bHeld)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		if (bHeld)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = 1.0f;
			FCataclysmStatInputs& Line =
				Inputs.FindOrAdd(FName(UCataclysmEquipmentComponent::BothHandsFullStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/**
	 * An actor with an ability system and the equipment it wears, so the
	 * equipment has an owner to read the option from. Nothing refreshes its
	 * stat line, so a flag given by hand stays given.
	 */
	struct FScopedWearer
	{
		FScopedWearer(UWorld* World, bool bHeld)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			Equipment = NewObject<UCataclysmEquipmentComponent>(Actor);
			Equipment->RegisterComponent();
			Inventory = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
			Hold(AbilitySystem, bHeld);
		}

		~FScopedWearer()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		FName In(ECataclysmGearSlot Slot) const
		{
			const FCataclysmItem* Item = Equipment->EquippedAt(Slot);
			return Item ? Item->Base : NAME_None;
		}

		void FillTheBag() const
		{
			while (!Inventory->IsFull())
			{
				Inventory->AddItem(Of(Boots));
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmInventoryComponent* Inventory = nullptr;
	};

	/** The sum of the flat modifiers the worn items give one stat. */
	float FlatFromWorn(const UCataclysmEquipmentComponent* Equipment, const TCHAR* Stat)
	{
		float Total = 0.0f;
		const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
			Equipment->GatherModifiers();
		if (const TArray<FCataclysmStatModifier>* Line = Modifiers.Find(FName(Stat)))
		{
			for (const FCataclysmStatModifier& Modifier : *Line)
			{
				if (Modifier.Bucket == ECataclysmStatBucket::Flat)
				{
					Total += Modifier.Value;
				}
			}
		}
		return Total;
	}

	/** How many hands a weapon type takes, from the base table, the way
	 *  `UCataclysmWeaponSlotsComponent::GetEquippedWeaponHands` reads it. */
	int32 HandsOfType(const FString& WeaponType)
	{
		int32 Found = -1;
		if (const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable())
		{
			Bases->ForeachRow<FCataclysmItemBaseRow>(
				TEXT("CataclysmBothHandsFullTest::HandsOfType"),
				[&](const FName&, const FCataclysmItemBaseRow& Row)
				{
					if (Found < 0
						&& Row.WeaponType.Equals(WeaponType, ESearchCase::IgnoreCase))
					{
						Found = Row.Hands;
					}
				});
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBothHandsFullSecondHandTest,
	"Cataclysm.BothHandsFull.WithTheOptionASecondTwoHandedWeaponGoesInTheOtherHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Without the option a second two-handed weapon replaces the first, as it
 * always has; with it, the second goes in the other hand and nothing comes off.
 */
bool FCataclysmBothHandsFullSecondHandTest::RunTest(const FString&)
{
	using namespace CataclysmBothHandsFullTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;

	// WITHOUT THE OPTION: THE CONTROL.
	FScopedWearer Plain(World, /*bHeld=*/false);
	Plain.Equipment->Equip(Of(Greatsword), Removed, AlsoRemoved, Slot);
	Removed = FCataclysmItem();
	Plain.Equipment->Equip(Of(Greataxe), Removed, AlsoRemoved, Slot);
	TestEqual(TEXT("without the option, the greataxe replaces the greatsword"),
			  Plain.In(ECataclysmGearSlot::Weapon1), FName(Greataxe));
	TestEqual(TEXT("and the greatsword is what came off"), Removed.Base, FName(Greatsword));
	TestEqual(TEXT("and the second hand is empty"),
			  Plain.In(ECataclysmGearSlot::Weapon2), FName(NAME_None));
	TestTrue(TEXT("and it is marked as taken by the one weapon"),
			 Plain.Equipment->TwoHandedOccupiesBothWeaponSlots());

	// WITH IT.
	FScopedWearer Held(World, /*bHeld=*/true);
	Held.Equipment->Equip(Of(Greatsword), Removed, AlsoRemoved, Slot);
	Removed = FCataclysmItem();
	AlsoRemoved = FCataclysmItem();
	const ECataclysmEquipResult Result =
		Held.Equipment->Equip(Of(Greataxe), Removed, AlsoRemoved, Slot);
	TestTrue(TEXT("with the option, the greataxe is put on with nothing coming off"),
			 Result == ECataclysmEquipResult::Equipped);
	TestEqual(TEXT("in the second hand"), static_cast<int32>(Slot),
			  static_cast<int32>(ECataclysmGearSlot::Weapon2));
	TestEqual(TEXT("the greatsword is still in the first"),
			  Held.In(ECataclysmGearSlot::Weapon1), FName(Greatsword));
	TestEqual(TEXT("and the greataxe in the second"),
			  Held.In(ECataclysmGearSlot::Weapon2), FName(Greataxe));
	TestTrue(TEXT("and nothing came off"),
			 Removed.Base.IsNone() && AlsoRemoved.Base.IsNone());
	TestFalse(TEXT("and neither weapon is marked as taking both hands"),
			  Held.Equipment->TwoHandedOccupiesBothWeaponSlots());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBothHandsFullBothCountTest,
	"Cataclysm.BothHandsFull.BothTwoHandedWeaponsAddTheirDamageAndTheirAffixes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Both contribute their damage, their affixes": the second greatsword adds
 * as much attack damage and as much of its affix as the first, so each is
 * exactly doubled. The x2 of a two-handed weapon is applied to each.
 */
bool FCataclysmBothHandsFullBothCountTest::RunTest(const FString&)
{
	using namespace CataclysmBothHandsFullTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedWearer Held(World, /*bHeld=*/true);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Held.Equipment->EquipInto(WithHealthAffix(Greatsword), ECataclysmGearSlot::Weapon1,
							  Removed, AlsoRemoved);
	const float DamageOfOne = FlatFromWorn(Held.Equipment, TEXT("attack_damage"));
	const float HealthOfOne = FlatFromWorn(Held.Equipment, TEXT("max_health"));
	if (!TestTrue(TEXT("one greatsword gives attack damage and its affix gives "
					   "maximum health, which every figure below depends on"),
				  DamageOfOne > 0.0f && HealthOfOne > 0.0f))
	{
		return false;
	}

	Held.Equipment->EquipInto(WithHealthAffix(Greatsword), ECataclysmGearSlot::Weapon2,
							  Removed, AlsoRemoved);
	TestEqual(TEXT("two give twice the attack damage"),
			  FlatFromWorn(Held.Equipment, TEXT("attack_damage")), DamageOfOne * 2.0f,
			  0.001f);
	TestEqual(TEXT("and twice the affix"),
			  FlatFromWorn(Held.Equipment, TEXT("max_health")), HealthOfOne * 2.0f,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBothHandsFullNoMixTest,
	"Cataclysm.BothHandsFull.NoLoadoutMixesATwoHandedWeaponWithAOneHandedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The design's four loadouts never put a two-handed and a one-handed weapon
 * together, with the option or without it. A two-handed weapon put beside a
 * one-handed one takes it off, and a one-handed weapon put over two two-handed
 * ones takes both off.
 */
bool FCataclysmBothHandsFullNoMixTest::RunTest(const FString&)
{
	using namespace CataclysmBothHandsFullTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedWearer Held(World, /*bHeld=*/true);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;

	Held.Equipment->EquipInto(Of(Sword), ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
	Removed = FCataclysmItem();
	Held.Equipment->EquipInto(Of(Greatsword), ECataclysmGearSlot::Weapon2,
							  Removed, AlsoRemoved);
	TestEqual(TEXT("a greatsword beside a sword takes the sword off"),
			  Removed.Base, FName(Sword));
	TestEqual(TEXT("leaving the first hand empty"),
			  Held.In(ECataclysmGearSlot::Weapon1), FName(NAME_None));
	TestEqual(TEXT("and the greatsword where it was put"),
			  Held.In(ECataclysmGearSlot::Weapon2), FName(Greatsword));

	Held.Equipment->EquipInto(Of(Greataxe), ECataclysmGearSlot::Weapon1,
							  Removed, AlsoRemoved);
	Removed = FCataclysmItem();
	AlsoRemoved = FCataclysmItem();
	Held.Equipment->EquipInto(Of(Dagger), ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
	// BOTH, AND NAMED, so the same weapon handed back twice would fail.
	const TArray<FName> CameOff = {Removed.Base, AlsoRemoved.Base};
	TestTrue(TEXT("a dagger put over two two-handed weapons takes the greataxe off"),
			 CameOff.Contains(FName(Greataxe)));
	TestTrue(TEXT("and the greatsword"), CameOff.Contains(FName(Greatsword)));
	TestEqual(TEXT("and only the dagger is held"), Held.Equipment->NumEquipped(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBothHandsFullTwoHandsTest,
	"Cataclysm.BothHandsFull.TheWeaponTheSkillsComeFromIsTwoHandedWithTwoAndWithOneInTheSecondHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-09-24: the Two Hands condition holds while ANY held weapon is
 * two-handed, and until the two-weapon skill pool (#837) the skills come from
 * the first weapon held. Both are read off `EquippedWeaponType`, which answers
 * the first occupied weapon slot, so with two-handed weapons in both hands, and
 * with one left only in the second hand, it has to name a two-handed type.
 *
 * WHAT THIS DOES NOT SHOW is `UCataclysmWeaponSlotsComponent` reading that type
 * on a real character; `HandsOfType` reads the base table the way its
 * `GetEquippedWeaponHands` does.
 */
bool FCataclysmBothHandsFullTwoHandsTest::RunTest(const FString&)
{
	using namespace CataclysmBothHandsFullTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedWearer Held(World, /*bHeld=*/true);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Held.Equipment->EquipInto(Of(Greatsword), ECataclysmGearSlot::Weapon1,
							  Removed, AlsoRemoved);
	Held.Equipment->EquipInto(Of(Greataxe), ECataclysmGearSlot::Weapon2,
							  Removed, AlsoRemoved);
	TestEqual(TEXT("with two, the skills come from the first hand's greatsword"),
			  Held.Equipment->EquippedWeaponType(), FString(TEXT("Greatsword")));
	TestEqual(TEXT("which takes two hands"),
			  HandsOfType(Held.Equipment->EquippedWeaponType()), 2);

	FCataclysmItem Taken;
	Held.Equipment->Unequip(ECataclysmGearSlot::Weapon1, Taken);
	TestEqual(TEXT("with only the second hand's greataxe, from the greataxe"),
			  Held.Equipment->EquippedWeaponType(), FString(TEXT("Greataxe")));
	TestEqual(TEXT("which takes two hands too"),
			  HandsOfType(Held.Equipment->EquippedWeaponType()), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBothHandsFullBagTest,
	"Cataclysm.BothHandsFull.WearingFromAFullBagAsksRoomOnlyWhenTwoComeOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A second two-handed weapon worn from a full bag beside a first takes nothing
 * off, so it needs no room and is worn. A one-handed weapon worn over two
 * two-handed ones takes both off, so it needs a free slot beyond the one it
 * leaves, and with none it is refused before anything moves.
 */
bool FCataclysmBothHandsFullBagTest::RunTest(const FString&)
{
	using namespace CataclysmBothHandsFullTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedWearer Held(World, /*bHeld=*/true);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Held.Equipment->EquipInto(Of(Greatsword), ECataclysmGearSlot::Weapon1,
							  Removed, AlsoRemoved);

	const int32 Carried = Held.Inventory->AddItem(Of(Greataxe));
	Held.FillTheBag();
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
	TestTrue(TEXT("a greataxe worn from a full bag beside a greatsword is worn"),
			 UCataclysmWearing::WearFromCarried(Held.Inventory, Held.Equipment, Carried,
												Slot)
				 == ECataclysmWearResult::Worn);
	TestEqual(TEXT("in the second hand"), Held.In(ECataclysmGearSlot::Weapon2),
			  FName(Greataxe));

	// THE GREATAXE LEFT ITS SLOT FREE, and a dagger goes in it, so the bag is
	// full again with the dagger carried.
	const int32 DaggerSlot = Held.Inventory->AddItem(Of(Dagger));
	TestTrue(TEXT("the bag is full with the dagger in it"), Held.Inventory->IsFull());
	TestTrue(TEXT("a dagger worn over two two-handed weapons from a full bag is "
				  "refused, since two would come off"),
			 UCataclysmWearing::WearFromCarried(Held.Inventory, Held.Equipment,
												DaggerSlot, Slot)
				 == ECataclysmWearResult::NoRoomInTheBag);
	TestEqual(TEXT("and both are still held"), Held.Equipment->NumEquipped(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBothHandsFullReturnTest,
	"Cataclysm.BothHandsFull.LosingTheOptionReturnsTheSecondToTheBagOrTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-09-24: when the option is lost, the two-handed weapon in the
 * second hand goes to the bag, or to the floor when the bag is full, and the
 * respec is never refused. On a real player the respec itself is what asks;
 * on a wearer the function is called directly, for the floor.
 */
bool FCataclysmBothHandsFullReturnTest::RunTest(const FString&)
{
	using namespace CataclysmBothHandsFullTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A REAL PLAYER AND A REAL RESPEC. It starts holding a greataxe.
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!State || !Controller || !Player)
	{
		AddError(TEXT("A possessed player could not be built."));
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	UCataclysmEquipmentComponent* Equipment = Player->GetEquipment();
	UCataclysmInventoryComponent* Bag = Player->GetInventory();
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	if (!TestTrue(TEXT("the player has equipment, a bag and an ability system"),
				  Equipment && Bag && System))
	{
		return false;
	}
	Equipment->UnequipEverything();
	TestEqual(TEXT("nothing is held to begin with"), Equipment->NumEquipped(), 0);

	// THE OPTION IS GIVEN JUST BEFORE EACH WEAPON GOES ON: putting one on
	// refreshes the stat line from the character's own sources, which do not
	// hold it yet.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Hold(System, true);
	Equipment->EquipInto(Of(Greatsword), ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
	Hold(System, true);
	Equipment->EquipInto(Of(Greataxe), ECataclysmGearSlot::Weapon2, Removed, AlsoRemoved);
	if (!TestEqual(TEXT("both are held before the respec"), Equipment->NumEquipped(), 2))
	{
		return false;
	}

	const int32 CarriedBefore = Bag->NumItems();
	State->ResetPassivePoints();
	TestEqual(TEXT("after a respec the greatsword is still in the first hand"),
			  Equipment->EquippedAt(ECataclysmGearSlot::Weapon1)
				  ? Equipment->EquippedAt(ECataclysmGearSlot::Weapon1)->Base
				  : FName(NAME_None),
			  FName(Greatsword));
	TestTrue(TEXT("and the second hand is empty"),
			 Equipment->SlotIsEmpty(ECataclysmGearSlot::Weapon2));
	TestEqual(TEXT("and the bag holds one more item"), Bag->NumItems(), CarriedBefore + 1);

	// WITH THE OPTION STILL HELD, NOTHING MOVES: THE CONTROL.
	FScopedWearer Wearer(World, /*bHeld=*/true);
	Wearer.Equipment->EquipInto(Of(Greatsword), ECataclysmGearSlot::Weapon1,
								Removed, AlsoRemoved);
	Wearer.Equipment->EquipInto(Of(Greataxe), ECataclysmGearSlot::Weapon2,
								Removed, AlsoRemoved);
	TestTrue(TEXT("with the option held, nothing is returned"),
			 UCataclysmWearing::ReturnSecondTwoHandedWeapon(
				 Wearer.Inventory, Wearer.Equipment, World, FVector::ZeroVector)
				 == ECataclysmWearResult::NothingWorn);
	TestEqual(TEXT("and both are still held"), Wearer.Equipment->NumEquipped(), 2);

	// WITH A FULL BAG, TO THE FLOOR.
	Hold(Wearer.AbilitySystem, false);
	Wearer.FillTheBag();
	int32 OnTheFloorBefore = 0;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		++OnTheFloorBefore;
	}
	TestTrue(TEXT("with the option gone and the bag full, it goes on the floor"),
			 UCataclysmWearing::ReturnSecondTwoHandedWeapon(
				 Wearer.Inventory, Wearer.Equipment, World, FVector(200.0f, 0.0f, 0.0f))
				 == ECataclysmWearResult::Dropped);
	int32 OnTheFloorAfter = 0;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		++OnTheFloorAfter;
	}
	TestEqual(TEXT("one more item lies on the floor"), OnTheFloorAfter,
			  OnTheFloorBefore + 1);
	TestTrue(TEXT("and the second hand is empty"),
			 Wearer.Equipment->SlotIsEmpty(ECataclysmGearSlot::Weapon2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBothHandsFullPanelTest,
	"Cataclysm.BothHandsFull.TheSecondHandIsMarkedTakenOnlyWithoutTheOption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The gear panel marks the second hand as taken by a two-handed weapon in the
 * first. With the option, one two-handed weapon leaves the second hand free
 * for another, so it is not marked; and with two, each hand shows its own.
 */
bool FCataclysmBothHandsFullPanelTest::RunTest(const FString&)
{
	using namespace CataclysmBothHandsFullTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;

	FScopedWearer Plain(World, /*bHeld=*/false);
	Plain.Equipment->EquipInto(Of(Greatsword), ECataclysmGearSlot::Weapon1,
							   Removed, AlsoRemoved);
	TestTrue(TEXT("without the option, one greatsword marks the second hand taken"),
			 UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Weapon2,
												Plain.Equipment));

	FScopedWearer Held(World, /*bHeld=*/true);
	Held.Equipment->EquipInto(Of(Greatsword), ECataclysmGearSlot::Weapon1,
							  Removed, AlsoRemoved);
	TestFalse(TEXT("with the option, one greatsword leaves the second hand free"),
			  UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Weapon2,
												 Held.Equipment));
	Held.Equipment->EquipInto(Of(Greataxe), ECataclysmGearSlot::Weapon2,
							  Removed, AlsoRemoved);
	TestFalse(TEXT("and with two, the second hand is not marked taken"),
			  UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Weapon2,
												 Held.Equipment));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
