// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"
#include "Tests/CataclysmTestRun.h"

/**
 * THE INVENTORY'S THREE TABS. Issue #2849, the owner's request of 2026-11-22 -- "I think we need to add tabs to the
 * inventory. One for crafting materials and one for gems. Otherwise you wont be able to carry any loot" -- and their
 * ruling the same day, "One cell per kind, no limit (Recommended)": the 48 slots hold gear, and crafting materials and
 * gems each have a store of their own, one stack per kind, that never fills.
 * `Cataclysm.InventoryTabs.` IS A NEW GROUP and selects these seven and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Every figure below is reasoned from the code, and the run that proves them is registered in
 * `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmInventoryTabsTest
{
	const FName Mote(TEXT("Material_Corrupted_Mote"));
	const FName Ash(TEXT("Material_Whispering_Ash"));
	const FName Life(TEXT("Gem_Of_Life"));
	const FName Viper(TEXT("Gem_Of_The_Viper"));
	const FName Rot(TEXT("Gem_Of_Rot"));

	/** A piece of gear with a made-up base: the bag stores any base, and these tests ask nothing of the table. */
	FCataclysmItem Filler(int32 N)
	{
		FCataclysmItem Item;
		Item.Base = FName(*FString::Printf(TEXT("Filler%d"), N));
		return Item;
	}

	/** Every gear slot from the first free one filled, so the gear tab is full. */
	void FillTheGearTab(UCataclysmInventoryComponent& Bag)
	{
		int32 N = 0;
		while (Bag.AddItem(Filler(N)) != INDEX_NONE)
		{
			++N;
		}
	}

	struct FTabsScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && Character && Character->GetInventory() && Character->GetEquipment() && Run
				&& Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }
	};

	/**
	 * A world with this project's game instance, a possessed Solo Self-Found player with gold of its own, and a run;
	 * nothing carried, nothing worn. The scene `CataclysmForgeGemsTests.cpp` builds, copied into this file's namespace
	 * because the module is built in unity files.
	 */
	FTabsScene MakeScene()
	{
		FTabsScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);
		ACataclysmPlayerState* State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (State && Controller && Out.Character)
		{
			Controller->SetPlayerState(State);
			Controller->Possess(Out.Character);
			FCataclysmCharacterPartition Partition;
			Partition.Lethality = ECataclysmLethality::Standard;
			Partition.Population = ECataclysmPopulation::Offline;
			Partition.bSoloSelfFound = true;
			State->SetIdentity(TEXT("Quartermaster"), Partition);
			State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Out.Run->Begin(/* InSeed */ 2849, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}

	/** A record read from JSON text through the save's own loader, which runs every migration step. */
	const UCataclysmCharacterSave* ReadCharacter(FAutomationTestBase& Test, const FString& Text, const TCHAR* Name)
	{
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name, FCataclysmSaveStorage::Describe(Result),
										  *Message));
		}
		return Read;
	}

	/** A carried slot's JSON as the record writes one: no item, and one stack or none. */
	TSharedRef<FJsonObject> StackEntry(const TSharedRef<FJsonObject>& Like, const TCHAR* Material, const TCHAR* Gem,
									   const TCHAR* Rarity, double Quantity)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->Values = Like->Values;
		Out->SetStringField(TEXT("Material"), Material);
		Out->SetStringField(TEXT("Gem"), Gem);
		Out->SetStringField(TEXT("GemRarity"), Rarity);
		Out->SetNumberField(TEXT("Quantity"), Quantity);
		return Out;
	}

	FString TextOf(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	/** "Gem_Of_Life Quality x1, ..." for an AddInfo line. */
	FString Describe(const TArray<FCataclysmCarriedSlot>& Stacks)
	{
		TArray<FString> Each;
		for (const FCataclysmCarriedSlot& Stack : Stacks)
		{
			Each.Add(FString::Printf(TEXT("%s %d x%d"),
									 Stack.Gem.IsNone() ? *Stack.Material.ToString() : *Stack.Gem.ToString(),
									 static_cast<int32>(Stack.GemRarity), Stack.Quantity));
		}
		return FString::Join(Each, TEXT(", "));
	}
}

// --------------------------------------------------------------------------------------------------------------------
// IT1. The issue's own "done when": a full gear tab still collects materials and gems.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryTabsFullGearTest,
	"Cataclysm.InventoryTabs.AFullGearTabStillCollectsMaterialsAndGems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * All 48 gear slots full. A Corrupted Mote and a Superb Of The Viper lying on the floor are both taken, their counts
 * rise, the gear tab is still full, and both drops are gone from the floor. CONTROL: a piece of gear is still refused,
 * and stays where it lies. Prints "TABS FULL GEAR".
 */
bool FCataclysmInventoryTabsFullGearTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTabsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: a bag"), Bag))
	{
		return false;
	}
	FillTheGearTab(*Bag);

	ACataclysmDroppedItem* Material = World->SpawnActor<ACataclysmDroppedItem>(FVector(100.0f, 0.0f, 0.0f),
																			   FRotator::ZeroRotator);
	ACataclysmDroppedItem* Gem = World->SpawnActor<ACataclysmDroppedItem>(FVector(200.0f, 0.0f, 0.0f),
																		  FRotator::ZeroRotator);
	ACataclysmDroppedItem* Gear = World->SpawnActor<ACataclysmDroppedItem>(FVector(300.0f, 0.0f, 0.0f),
																		   FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: three drops"), Material && Gem && Gear)
		|| !TestTrue(TEXT("set-up: the gear tab is full"), Bag->IsFull() && Bag->NumFreeSlots() == 0))
	{
		return false;
	}
	Material->Material = Mote;
	Material->MaterialQuantity = 2;
	Material->MaterialTier = 1;
	Material->DisplayName = TEXT("Corrupted Mote");
	Gem->Gem = Viper;
	Gem->GemRarity = ECataclysmRarity::Superb;
	Gem->GemQuantity = 1;
	Gem->DisplayName = TEXT("Of The Viper");
	Gear->Item = Filler(99);
	Gear->DisplayName = TEXT("Filler");

	const bool bMaterialTaken = UCataclysmDropPickup::TakeInto(Bag, Material);
	const bool bGemTaken = UCataclysmDropPickup::TakeInto(Bag, Gem);
	const bool bGearTaken = UCataclysmDropPickup::TakeInto(Bag, Gear);
	AddInfo(FString::Printf(TEXT("TABS FULL GEAR: material taken %d (%d carried), gem taken %d (%d carried), gear taken %d; "
								 "%d gear slots free"),
							bMaterialTaken, Bag->CountOfMaterial(Mote), bGemTaken,
							Bag->CountOfGems(Viper, ECataclysmRarity::Superb), bGearTaken, Bag->NumFreeSlots()));

	TestTrue(TEXT("**the material is taken with every gear slot full**"), bMaterialTaken);
	TestEqual(TEXT("**and two are carried**"), Bag->CountOfMaterial(Mote), 2);
	TestTrue(TEXT("**the gem is taken too**"), bGemTaken);
	TestEqual(TEXT("**and one is carried**"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 1);
	TestFalse(TEXT("both drops are gone from the floor"), IsValid(Material) || IsValid(Gem));
	TestEqual(TEXT("the gear tab is still full"), Bag->NumFreeSlots(), 0);
	TestFalse(TEXT("CONTROL: a piece of gear is still refused"), bGearTaken);
	TestTrue(TEXT("CONTROL: and stays where it lies"), IsValid(Gear));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IT2. A stack takes no gear slot; the gems tab is sorted by gem, then rarity.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryTabsStoresTest,
	"Cataclysm.InventoryTabs.AStackTakesNoGearSlotAndTheGemsAreSortedByGemThenRarity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An empty bag given 5 Corrupted Mote, 3 more, 7 Whispering Ash and gems in the order Superb Of The Viper, Quality Of
 * Life, Everyday Of The Viper: no gear slot is used, the first piece of gear still goes to slot 0, the materials tab
 * holds two stacks (8 and 7) and the gems tab three, in the order Quality Of Life, Everyday Of The Viper, Superb Of The
 * Viper. Every add and take raises the change count. Taking all 8 Mote takes its stack out; taking a ninth is refused.
 * A name of None and a quantity of 0 are refused. Prints "TABS STORES".
 */
bool FCataclysmInventoryTabsStoresTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTabsTest;
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: a bag"), Bag))
	{
		return false;
	}
	const int32 Before = Bag->ChangeCount();
	const bool bAdded = Bag->AddMaterial(Mote, 5) && Bag->AddMaterial(Mote, 3) && Bag->AddMaterial(Ash, 7)
		&& Bag->AddGems(Viper, ECataclysmRarity::Superb, 1) && Bag->AddGems(Life, ECataclysmRarity::Quality, 2)
		&& Bag->AddGems(Viper, ECataclysmRarity::Everyday, 4);
	const int32 AfterAdding = Bag->ChangeCount();
	const int32 Used = Bag->NumItems();
	const int32 Free = Bag->NumFreeSlots();
	const int32 First = Bag->FirstFreeSlot();
	const TArray<FCataclysmCarriedSlot> Materials = Bag->GetMaterialStacks();
	const TArray<FCataclysmCarriedSlot> Gems = Bag->GetGemStacks();
	const int32 GearAt = Bag->AddItem(Filler(0));
	AddInfo(FString::Printf(TEXT("TABS STORES: added %d; %d gear used, %d free, first free %d; materials %s; gems %s; gear went "
								 "to %d; changes %d to %d"),
							bAdded, Used, Free, First, *Describe(Materials), *Describe(Gems), GearAt, Before,
							AfterAdding));

	TestTrue(TEXT("set-up: every add answers true"), bAdded);
	TestEqual(TEXT("**no gear slot is used by a stack**"), Used, 0);
	TestEqual(TEXT("**all 48 are free**"), Free, 48);
	TestEqual(TEXT("and the first free is 0"), First, 0);
	TestEqual(TEXT("the first piece of gear goes to slot 0"), GearAt, 0);
	TestEqual(TEXT("each add raises the change count"), AfterAdding - Before, 6);
	if (TestEqual(TEXT("**two material stacks**"), Materials.Num(), 2))
	{
		TestTrue(TEXT("8 Corrupted Mote first, as first carried"), Materials[0].Material == Mote && Materials[0].Quantity == 8);
		TestTrue(TEXT("then 7 Whispering Ash"), Materials[1].Material == Ash && Materials[1].Quantity == 7);
	}
	if (TestEqual(TEXT("**three gem stacks**"), Gems.Num(), 3))
	{
		TestTrue(TEXT("**sorted: Quality Of Life first**"),
				 Gems[0].Gem == Life && Gems[0].GemRarity == ECataclysmRarity::Quality && Gems[0].Quantity == 2);
		TestTrue(TEXT("**then Everyday Of The Viper**"),
				 Gems[1].Gem == Viper && Gems[1].GemRarity == ECataclysmRarity::Everyday && Gems[1].Quantity == 4);
		TestTrue(TEXT("**then Superb Of The Viper**"),
				 Gems[2].Gem == Viper && Gems[2].GemRarity == ECataclysmRarity::Superb && Gems[2].Quantity == 1);
	}

	TestFalse(TEXT("a ninth Mote cannot be taken from eight"), Bag->RemoveMaterial(Mote, 9));
	TestTrue(TEXT("all eight can"), Bag->RemoveMaterial(Mote, 8));
	TestEqual(TEXT("and its stack leaves the tab"), Bag->GetMaterialStacks().Num(), 1);
	TestEqual(TEXT("none is carried"), Bag->CountOfMaterial(Mote), 0);
	TestFalse(TEXT("a material with no name is refused"), Bag->AddMaterial(NAME_None, 1));
	TestFalse(TEXT("none of a material is refused"), Bag->AddMaterial(Mote, 0));
	TestFalse(TEXT("a gem with no name is refused"), Bag->AddGems(NAME_None, ECataclysmRarity::Everyday, 1));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IT3. A version 14 record's stacks move into the two tabs; its gear keeps its cell.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryTabsMigrationTest,
	"Cataclysm.InventoryTabs.AVersion14RecordsStacksMoveIntoTheTabsAndItsGearKeepsItsCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Character_v14.json` read through the loader, which runs `Migrate_14_to_15` and `Migrate_15_to_16`: its Circlet stays in slot 0, its 12
 * Whispering Ash leave slot 1 for the materials tab and slot 1 is blank but still there, and the gems tab is empty.
 * THE SAME FILE WITH THREE MORE SLOTS -- 5 more Whispering Ash, 2 Superb Of The Viper and a stack of 0 Of Rot -- moves
 * 17 Whispering Ash as one stack and 2 Viper, drops the empty stack, and keeps five slots. Prints "TABS MIGRATED".
 */
bool FCataclysmInventoryTabsMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTabsTest;
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v14.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const UCataclysmCharacterSave* Plain = ReadCharacter(*this, Text, TEXT("Character_v14.json"));

	// THE SAME FILE WITH THREE MORE SLOTS, built from its second slot so each has every field a slot writes.
	const TSharedPtr<FJsonObject> Object = CataclysmSaveFixtures::Parse(Text);
	const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
	if (!TestTrue(TEXT("set-up: the file parses"), Object.IsValid())
		|| !TestTrue(TEXT("set-up: and lists its carried slots"),
					 Object->TryGetArrayField(TEXT("CarriedSlots"), Slots) && Slots && Slots->Num() == 2))
	{
		return false;
	}
	const TSharedRef<FJsonObject> Second = (*Slots)[1]->AsObject().ToSharedRef();
	TArray<TSharedPtr<FJsonValue>> More = *Slots;
	More.Add(MakeShared<FJsonValueObject>(StackEntry(Second, TEXT("Whispering_Ash"), TEXT("None"), TEXT("Everyday"), 5.0)));
	More.Add(MakeShared<FJsonValueObject>(StackEntry(Second, TEXT("None"), TEXT("Gem_Of_The_Viper"), TEXT("Superb"), 2.0)));
	More.Add(MakeShared<FJsonValueObject>(StackEntry(Second, TEXT("None"), TEXT("Gem_Of_Rot"), TEXT("Quality"), 0.0)));
	Object->SetArrayField(TEXT("CarriedSlots"), More);
	const UCataclysmCharacterSave* Busy = ReadCharacter(*this, TextOf(Object.ToSharedRef()), TEXT("the busier file"));
	if (!Plain || !Busy)
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("TABS MIGRATED: version %d; %d slots, slot 0 %s, slot 1 %s x%d; materials %s; gems %s | the "
								 "busier file %d slots; materials %s; gems %s"),
							Plain->SchemaVersion, Plain->CarriedSlots.Num(),
							Plain->CarriedSlots.IsValidIndex(0) ? *Plain->CarriedSlots[0].Item.Base.ToString() : TEXT("-"),
							Plain->CarriedSlots.IsValidIndex(1) ? *Plain->CarriedSlots[1].Material.ToString() : TEXT("-"),
							Plain->CarriedSlots.IsValidIndex(1) ? Plain->CarriedSlots[1].Quantity : -1,
							*Describe(Plain->CarriedMaterials), *Describe(Plain->CarriedGems), Busy->CarriedSlots.Num(),
							*Describe(Busy->CarriedMaterials), *Describe(Busy->CarriedGems)));

	TestEqual(TEXT("it arrives at the current version"), Plain->SchemaVersion, UCataclysmCharacterSave::SchemaVersionNow);
	if (!TestEqual(TEXT("**its two slots are both still there**"), Plain->CarriedSlots.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("**the Circlet keeps slot 0**"), Plain->CarriedSlots[0].Item.Base == FName(TEXT("Circlet")));
	TestTrue(TEXT("**slot 1 is blank**"), UCataclysmInventoryComponent::SlotIsEmpty(Plain->CarriedSlots[1])
				 && Plain->CarriedSlots[1].Material.IsNone() && Plain->CarriedSlots[1].Quantity == 0);
	TestTrue(TEXT("**the 12 Whispering Ash are in the materials tab**"),
			 Plain->CarriedMaterials.Num() == 1 && Plain->CarriedMaterials[0].Material == FName(TEXT("Whispering_Ash"))
				 && Plain->CarriedMaterials[0].Quantity == 12);
	TestEqual(TEXT("and the gems tab is empty"), Plain->CarriedGems.Num(), 0);

	TestEqual(TEXT("the busier file keeps all five slots"), Busy->CarriedSlots.Num(), 5);
	TestTrue(TEXT("**two stacks of one material are one: 17 Whispering Ash**"),
			 Busy->CarriedMaterials.Num() == 1 && Busy->CarriedMaterials[0].Quantity == 17);
	TestTrue(TEXT("**the Viper is in the gems tab at Superb, 2 of them**"),
			 Busy->CarriedGems.Num() == 1 && Busy->CarriedGems[0].Gem == Viper
				 && Busy->CarriedGems[0].GemRarity == ECataclysmRarity::Superb && Busy->CarriedGems[0].Quantity == 2);
	bool bAllBlank = true;
	for (int32 Slot = 1; Slot < Busy->CarriedSlots.Num(); ++Slot)
	{
		bAllBlank &= UCataclysmInventoryComponent::SlotIsEmpty(Busy->CarriedSlots[Slot])
			&& Busy->CarriedSlots[Slot].Gem.IsNone() && Busy->CarriedSlots[Slot].Material.IsNone();
	}
	TestTrue(TEXT("and every slot a stack left, the empty Rot's included, is blank"), bAllBlank);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IT4. Gathered and restored, both tabs come back; a stack found in a saved gear slot goes to its tab.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryTabsRestoreTest,
	"Cataclysm.InventoryTabs.RestoringPutsBothTabsBackAndAStackInASavedSlotGoesToItsTab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A bag of one piece of gear, 4 Corrupted Mote and 2 Quality Of Life, gathered as the save gathers it and restored into
 * a second bag: the same piece in slot 0, the same 4 and 2. THEN A SAVED SLOT HOLDING 6 WHISPERING ASH at slot 3,
 * restored after the tabs: the 6 go to the materials tab, beside the 4 Mote, and slot 3 is empty. Prints "TABS
 * RESTORED".
 */
bool FCataclysmInventoryTabsRestoreTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTabsTest;
	UCataclysmInventoryComponent* From = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	UCataclysmInventoryComponent* Into = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestTrue(TEXT("set-up: two bags"), From && Into))
	{
		return false;
	}
	From->AddItem(Filler(7));
	From->AddMaterial(Mote, 4);
	From->AddGems(Life, ECataclysmRarity::Quality, 2);

	TArray<FCataclysmCarriedSlot> Slots;
	TArray<FCataclysmCarriedSlot> Materials;
	TArray<FCataclysmCarriedSlot> Gems;
	FCataclysmSaveGather::CarriedSlotsFrom(*From, Slots);
	FCataclysmSaveGather::CarriedStacksFrom(*From, Materials, Gems);
	Into->RestoreStacks(Materials, Gems);
	const bool bRestored = Into->RestoreSlots(Slots);
	const FCataclysmItem* Piece = Into->ItemAt(0);
	const int32 MoteBack = Into->CountOfMaterial(Mote);
	const int32 LifeBack = Into->CountOfGems(Life, ECataclysmRarity::Quality);

	TArray<FCataclysmCarriedSlot> WithAStack = Slots;
	WithAStack[3].Material = Ash;
	WithAStack[3].Quantity = 6;
	Into->RestoreStacks(Materials, Gems);
	const bool bRestoredWithAStack = Into->RestoreSlots(WithAStack);
	AddInfo(FString::Printf(TEXT("TABS RESTORED: %d slots gathered, %d material and %d gem stacks; restored %d: slot 0 %s, "
								 "%d Mote, %d Life | with a stack in slot 3: restored %d, %d Ash, %d Mote, slot 3 empty %d"),
							Slots.Num(), Materials.Num(), Gems.Num(), bRestored,
							Piece ? *Piece->Base.ToString() : TEXT("-"), MoteBack, LifeBack, bRestoredWithAStack,
							Into->CountOfMaterial(Ash), Into->CountOfMaterial(Mote), Into->ItemAt(3) == nullptr
								&& UCataclysmInventoryComponent::SlotIsEmpty(Into->GetSlots()[3])));

	TestEqual(TEXT("set-up: 48 slots gathered"), Slots.Num(), UCataclysmInventoryComponent::SlotCount);
	TestTrue(TEXT("set-up: one stack of each kind gathered"), Materials.Num() == 1 && Gems.Num() == 1);
	TestTrue(TEXT("restored"), bRestored);
	TestTrue(TEXT("**the piece is back in slot 0**"), Piece != nullptr && Piece->Base == Filler(7).Base);
	TestEqual(TEXT("**the 4 Mote are back**"), MoteBack, 4);
	TestEqual(TEXT("**the 2 Quality Of Life are back**"), LifeBack, 2);
	TestTrue(TEXT("restored with a stack in a saved slot"), bRestoredWithAStack);
	TestEqual(TEXT("**the 6 Whispering Ash went to the materials tab**"), Into->CountOfMaterial(Ash), 6);
	TestEqual(TEXT("beside the 4 Mote"), Into->CountOfMaterial(Mote), 4);
	TestTrue(TEXT("**and slot 3 is empty**"), UCataclysmInventoryComponent::SlotIsEmpty(Into->GetSlots()[3]));
	TestEqual(TEXT("one piece of gear is carried"), Into->NumItems(), 1);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IT5. The forge combines and unsockets with every gear slot full.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryTabsForgeTest,
	"Cataclysm.InventoryTabs.TheForgeCombinesAndUnsocketsWithEveryGearSlotFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A helm with a Superb Of The Viper in its second socket in slot 0, and the other 47 gear slots full. Two +1 stones
 * combine into a +2, two Everyday Of Life into a Quality, and unsocketing the Viper with the one Dismantling Dust carried
 * is offered: until #2849 each of these was refused when no gear slot was free and no stack of the made thing was
 * carried (the unsocket allowed it only because the last Dust would free a cell). Prints "TABS FORGE".
 */
bool FCataclysmInventoryTabsForgeTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTabsTest;
	FTabsScene Scene = MakeScene();
	ON_SCOPE_EXIT { if (Scene.World) { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); } };
	if (!TestTrue(TEXT("set-up: a world, a player and a run"), Scene.IsUsable()))
	{
		return false;
	}
	FCataclysmItem Helm;
	Helm.Base = FName(TEXT("Head_Helm"));
	FCataclysmSocket Empty;
	Empty.Type = ECataclysmSocketType::Defense;
	FCataclysmSocket Holding;
	Holding.Type = ECataclysmSocketType::Attack;
	Holding.Gem = Viper;
	Holding.GemRarity = ECataclysmRarity::Superb;
	Helm.Sockets = {Empty, Holding};
	const int32 HelmAt = Scene.Bag()->AddItem(Helm);
	FillTheGearTab(*Scene.Bag());
	const bool bStacks = Scene.Bag()->AddMaterial(UCataclysmForge::StoneFor(1), 2)
		&& Scene.Bag()->AddGems(Life, ECataclysmRarity::Everyday, 2)
		&& Scene.Bag()->AddMaterial(FName(UCataclysmForge::DismantlingDustRow), 1);
	if (!TestEqual(TEXT("set-up: the helm is in slot 0"), HelmAt, 0)
		|| !TestEqual(TEXT("set-up: and every gear slot is full"), Scene.Bag()->NumFreeSlots(), 0)
		|| !TestTrue(TEXT("set-up: two +1 stones, two Everyday Of Life and one Dismantling Dust carried"), bStacks))
	{
		return false;
	}

	FCataclysmForgeCombine Stone;
	FString StoneReason;
	const bool bStone = UCataclysmForgeCraft::Combine(Scene.World, 1, Stone, StoneReason);
	FCataclysmForgeGemCombine Gem;
	FString GemReason;
	const bool bGem = UCataclysmForgeCraft::CombineGems(Scene.World, Life, ECataclysmRarity::Everyday, Gem, GemReason);
	const FCataclysmForgeSocketing Unsocket =
		UCataclysmForgeCraft::UnsocketPlanFor(Scene.World, FCataclysmForgeTarget::Carried(0), 1);
	AddInfo(FString::Printf(TEXT("TABS FORGE: stone combine %d (%s), %d +2 held; gem combine %d (%s), %d Quality Of Life held; "
								 "unsocket offered %d (%s)"),
							bStone, *StoneReason, Scene.Bag()->CountOfMaterial(UCataclysmForge::StoneFor(2)), bGem,
							*GemReason, Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Quality), Unsocket.bPossible,
							*Unsocket.Refusal));

	TestTrue(TEXT("**two +1 stones combine with every gear slot full**"), bStone);
	TestEqual(TEXT("and a +2 is carried"), Scene.Bag()->CountOfMaterial(UCataclysmForge::StoneFor(2)), 1);
	TestTrue(TEXT("**two Everyday Of Life combine too**"), bGem);
	TestEqual(TEXT("and a Quality Of Life is carried"), Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Quality), 1);
	TestTrue(TEXT("**unsocketing the Viper is offered**"), Unsocket.bPossible);
	TestEqual(TEXT("CONTROL: the gear tab is still full"), Scene.Bag()->NumFreeSlots(), 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IT6. The screen's words: the tab names, and every material by tier.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryTabsScreenTest,
	"Cataclysm.InventoryTabs.TheScreenNamesTheTabsAndShowsEveryMaterialByTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Materials (12)" and "Gems (3)", and no name for the gear tab, whose name is the header line. The materials tab lists
 * every row of the crafting material table, 27 of them, by tier with none lower after a higher, the Corrupted Mote
 * among them named as the table names it; a material held at nought says "0". The tab shown is drawn in another colour
 * from the two that are not. Prints "TABS SCREEN".
 */
bool FCataclysmInventoryTabsScreenTest::RunTest(const FString&)
{
	const UDataTable* Table = UCataclysmDropRoll::LoadCraftingMaterialTable();
	if (!TestNotNull(TEXT("set-up: the crafting material table"), Table))
	{
		return false;
	}
	const TArray<FName> Order = UCataclysmInventoryScreen::MaterialTabOrder(Table);
	bool bByTier = true;
	int32 Previous = 0;
	for (const FName& Material : Order)
	{
		const int32 Tier = UCataclysmDropRoll::MaterialTierOf(Table, Material);
		bByTier &= Tier >= Previous;
		Previous = Tier;
	}
	const FString MoteName =
		UCataclysmInventoryScreen::MaterialTabLabelFor(CataclysmInventoryTabsTest::Mote, Table);
	AddInfo(FString::Printf(TEXT("TABS SCREEN: \"%s\", \"%s\", gear \"%s\"; %d materials, by tier %d, first %s; the Mote is "
								 "\"%s\"; nought says \"%s\""),
							*UCataclysmInventoryScreen::TabNameFor(ECataclysmInventoryTab::Materials, 12),
							*UCataclysmInventoryScreen::TabNameFor(ECataclysmInventoryTab::Gems, 3),
							*UCataclysmInventoryScreen::TabNameFor(ECataclysmInventoryTab::Gear, 5), Order.Num(), bByTier,
							Order.IsEmpty() ? TEXT("-") : *Order[0].ToString(), *MoteName,
							*UCataclysmInventoryScreen::MaterialTabCountFor(0)));

	TestEqual(TEXT("**the materials tab's name**"),
			  UCataclysmInventoryScreen::TabNameFor(ECataclysmInventoryTab::Materials, 12), FString(TEXT("Materials (12)")));
	TestEqual(TEXT("**the gems tab's name**"), UCataclysmInventoryScreen::TabNameFor(ECataclysmInventoryTab::Gems, 3),
			  FString(TEXT("Gems (3)")));
	TestTrue(TEXT("the gear tab's name is the header line, not this"),
			 UCataclysmInventoryScreen::TabNameFor(ECataclysmInventoryTab::Gear, 5).IsEmpty());
	TestEqual(TEXT("**every material in the table, 27**"), Order.Num(), Table->GetRowNames().Num());
	TestEqual(TEXT("and the table holds 27"), Table->GetRowNames().Num(), 27);
	TestTrue(TEXT("**by tier, lowest first**"), bByTier);
	TestTrue(TEXT("the Corrupted Mote is among them"), Order.Contains(CataclysmInventoryTabsTest::Mote));
	TestEqual(TEXT("named as the table names it"), MoteName, FString(TEXT("Corrupted Mote")));
	TestEqual(TEXT("**a material held at nought says 0**"), UCataclysmInventoryScreen::MaterialTabCountFor(0),
			  FString(TEXT("0")));
	TestFalse(TEXT("the tab shown is in another colour"),
			  UCataclysmInventoryScreen::TabColourFor(true).Equals(UCataclysmInventoryScreen::TabColourFor(false)));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IT7. The gems tab pages: 200 stacks are six pages of 36, and page 2 begins with the 37th.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryTabsPagesTest,
	"Cataclysm.InventoryTabs.TheGemsTabPagesItsStacksAsManyRowsAPageAsTheMaterialsTabUses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The coordinating session's addition of 2026-11-22: 224 gem stacks are reachable, and a tab running off the screen
 * would hide gems, so the gems tab pages. With the crafting material table's 27 materials the materials tab uses 3 rows
 * of twelve, so a page is 36 stacks. A bag of 200 stacks, Gem000 to Gem199 at Everyday, is 6 pages; page 2 begins at
 * place 36, which is Gem036, the 37th; page 6 holds the last 20. Page 0 and page 9 are held to 1 and 6, and an empty tab
 * is page 1 of 1. Prints "TABS PAGES".
 */
bool FCataclysmInventoryTabsPagesTest::RunTest(const FString&)
{
	const UDataTable* Table = UCataclysmDropRoll::LoadCraftingMaterialTable();
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: the crafting material table"), Table) || !TestNotNull(TEXT("set-up: a bag"), Bag))
	{
		return false;
	}
	// NO UNDERSCORE BEFORE THE DIGITS. An FName keeps a trailing "_<number>" without a leading zero as a number apart
	// from its text, and `FName::LexicalLess`, the tab's order, compares the text first: Gem_100 to Gem_199 are the
	// text "Gem" and sorted before Gem_000 to Gem_099, so place 36 held Gem_136 (window inv-w1, 2026-11-24). No real
	// gem name ends in a number (game/Data/Gems.csv: Gem_Of_The_Viper and the like).
	for (int32 N = 0; N < 200; ++N)
	{
		Bag->AddGems(FName(*FString::Printf(TEXT("Gem%03d"), N)), ECataclysmRarity::Everyday, 1);
	}
	using FScreen = UCataclysmInventoryScreen;
	const int32 PerPage = FScreen::GemStacksPerPage(Table->GetRowNames().Num());
	const int32 Stacks = Bag->GetGemStacks().Num();
	const int32 Pages = FScreen::GemPageCount(Stacks, PerPage);
	const int32 SecondStart = FScreen::GemPageStart(2, PerPage);
	const int32 LastStart = FScreen::GemPageStart(Pages, PerPage);
	const FName SecondFirst =
		Bag->GetGemStacks().IsValidIndex(SecondStart) ? Bag->GetGemStacks()[SecondStart].Gem : NAME_None;
	AddInfo(FString::Printf(TEXT("TABS PAGES: %d stacks, %d a page, %d pages; page 2 starts at %d with %s; the last at %d, "
								 "holding %d; \"%s\"; 0 held to %d, 9 to %d; none is %d page(s)"),
							Stacks, PerPage, Pages, SecondStart, *SecondFirst.ToString(), LastStart, Stacks - LastStart,
							*FScreen::GemPageText(2, Pages), FScreen::ClampGemPage(0, Pages),
							FScreen::ClampGemPage(9, Pages), FScreen::GemPageCount(0, PerPage)));

	TestEqual(TEXT("set-up: 200 stacks"), Stacks, 200);
	TestEqual(TEXT("**36 stacks a page: 3 rows of twelve, as the materials tab's 27 use**"), PerPage, 36);
	TestEqual(TEXT("**6 pages**"), Pages, 6);
	TestEqual(TEXT("**page 2 begins at place 36**"), SecondStart, 36);
	TestTrue(TEXT("**which is the 37th stack, Gem036**"), SecondFirst == FName(TEXT("Gem036")));
	TestEqual(TEXT("the last page holds the last 20"), Stacks - LastStart, 20);
	TestEqual(TEXT("**its words**"), FScreen::GemPageText(2, Pages), FString(TEXT("Page 2 of 6")));
	TestEqual(TEXT("page 0 is held to 1"), FScreen::ClampGemPage(0, Pages), 1);
	TestEqual(TEXT("page 9 is held to 6"), FScreen::ClampGemPage(9, Pages), 6);
	TestEqual(TEXT("an empty tab is one page"), FScreen::GemPageCount(0, PerPage), 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
