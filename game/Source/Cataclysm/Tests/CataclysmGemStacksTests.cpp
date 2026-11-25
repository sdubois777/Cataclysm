// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmWearing.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "UObject/Package.h"

/**
 * GEMS AS ITEMS: CARRIED IN STACKS, ONE PER GEM AND RARITY. Issue #46, slice G2.
 *
 * Ruled by the coordinating session on 2026-11-21: gems stack, one stack per gem and rarity, for the reason crafting
 * materials stack. A gem stack shows its name, its count, its rarity's colour and border, and a tooltip naming its
 * rarity and type, its effect at that rarity and the socket it fits. Since issue #2849 the stacks are the inventory's
 * gems tab, apart from the 48 gear slots, with no limit, and a stack cannot be dropped (slice G5 had offered a dropped
 * stack to the floor whole, as one gem drop; G2 refused to drop gems at all).
 *
 * The inventory is made bare, as `CataclysmDropPickupTests.cpp` makes one: nothing here needs a world. The names and
 * effects are read from the generated `DT_Gems`.
 */
namespace CataclysmGemStacksTest
{
	const FName Viper(TEXT("Gem_Of_The_Viper"));
	const FName Rot(TEXT("Gem_Of_Rot"));
	const FName Mote(TEXT("Material_Corrupted_Mote"));

	UCataclysmInventoryComponent* NewBag()
	{
		return NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	}

	FCataclysmCarriedSlot GemSlot(FName Gem, ECataclysmRarity Rarity, int32 Quantity)
	{
		FCataclysmCarriedSlot Slot;
		Slot.Gem = Gem;
		Slot.GemRarity = Rarity;
		Slot.Quantity = Quantity;
		return Slot;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// GK1. Gems stack by gem and rarity, beside a material, and a gem stack is neither empty nor an item.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksStackTest,
	"Cataclysm.GemStacks.GemsStackByGemAndRarity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStacksStackTest::RunTest(const FString&)
{
	using namespace CataclysmGemStacksTest;
	UCataclysmInventoryComponent* Bag = NewBag();
	if (!TestNotNull(TEXT("set-up: a bag"), Bag))
	{
		return false;
	}

	// THE GEMS TAB, since issue #2849: one stack per gem and rarity, none of them in a gear slot.
	TestTrue(TEXT("two Superb Of The Viper are taken"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 2));
	TestTrue(TEXT("a third joins their stack"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 1));
	TestEqual(TEXT("so three are carried"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 3);
	TestEqual(TEXT("in one stack"), Bag->GetGemStacks().Num(), 1);
	TestTrue(TEXT("an Everyday Of The Viper is taken"), Bag->AddGems(Viper, ECataclysmRarity::Everyday, 1));
	TestEqual(TEXT("as a different stack"), Bag->GetGemStacks().Num(), 2);
	TestEqual(TEXT("and does not join the Superb one"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 3);
	TestTrue(TEXT("a material is taken"), Bag->AddMaterial(Mote, 5));
	TestEqual(TEXT("into the materials tab, not the gems tab"), Bag->GetMaterialStacks().Num(), 1);
	TestEqual(TEXT("and the gems tab still holds two stacks"), Bag->GetGemStacks().Num(), 2);
	TestEqual(TEXT("no gear slot is used"), Bag->NumItems(), 0);
	const FCataclysmCarriedSlot& First = Bag->GetGemStacks()[0];
	TestFalse(TEXT("a gem stack is not an empty slot"), UCataclysmInventoryComponent::SlotIsEmpty(First));
	TestTrue(TEXT("and holds no item"), First.Item.Base.IsNone());
	TestEqual(TEXT("the material is counted apart from the gems"), Bag->CountOfMaterial(Mote), 5);

	TestFalse(TEXT("CONTROL: a gem with no name is refused"), Bag->AddGems(NAME_None, ECataclysmRarity::Superb, 1));
	TestFalse(TEXT("CONTROL: none of a gem is refused"), Bag->AddGems(Rot, ECataclysmRarity::Superb, 0));
	TestEqual(TEXT("and nothing more was stored"), Bag->GetGemStacks().Num(), 2);
	TestEqual(TEXT("nor any Of Rot"), Bag->CountOfGems(Rot, ECataclysmRarity::Superb), 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GK2. A full gear tab still takes a gem nothing carries yet, and still adds to a stack it carries. Until issue #2849 a
// gem stack took one of the 48 slots, so a full bag refused a new gem; the gems tab has no limit and never refuses.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksFullBagTest,
	"Cataclysm.GemStacks.AFullGearTabStillTakesANewGemAndStacksACarriedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStacksFullBagTest::RunTest(const FString&)
{
	using namespace CataclysmGemStacksTest;
	UCataclysmInventoryComponent* Bag = NewBag();
	if (!TestNotNull(TEXT("set-up: a bag"), Bag)
		|| !TestTrue(TEXT("set-up: one Superb Of The Viper"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 1)))
	{
		return false;
	}
	FCataclysmItem Piece;
	Piece.Base = FName(TEXT("Test_Gem_Stack_Filler"));
	for (int32 Index = 0; Index < UCataclysmInventoryComponent::SlotCount; ++Index)
	{
		Bag->AddItem(Piece);
	}
	if (!TestTrue(TEXT("set-up: the gear tab is full"), Bag->IsFull())
		|| !TestEqual(TEXT("set-up: with 48 pieces, the gem taking none of the slots"), Bag->NumItems(),
					  UCataclysmInventoryComponent::SlotCount))
	{
		return false;
	}
	TestTrue(TEXT("a gem nothing carries is taken"), Bag->AddGems(Rot, ECataclysmRarity::Everyday, 1));
	TestEqual(TEXT("and one of it is carried"), Bag->CountOfGems(Rot, ECataclysmRarity::Everyday), 1);
	TestTrue(TEXT("a carried stack still has room"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 4));
	TestEqual(TEXT("so five are carried"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 5);
	TestEqual(TEXT("in two stacks"), Bag->GetGemStacks().Num(), 2);
	TestEqual(TEXT("CONTROL: and a piece of gear is still refused"), Bag->AddItem(Piece), INDEX_NONE);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GK3. A gem stack, and a material stack, cannot be dropped by a carried slot: since issue #2849 neither is in one, and
// there is deliberately no way to drop a stack (ruled 2026-11-22: a dropped stack would be collected straight back
// within 15 m). Until then a stack was offered to the floor from its slot, as slice G5 lifted G2's refusal.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksDropTest,
	"Cataclysm.GemStacks.AGemStackCannotBeDroppedFromACarriedSlotAndStaysCarried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStacksDropTest::RunTest(const FString&)
{
	using namespace CataclysmGemStacksTest;
	UCataclysmInventoryComponent* Bag = NewBag();
	if (!TestNotNull(TEXT("set-up: a bag"), Bag)
		|| !TestTrue(TEXT("set-up: three Superb Of The Viper"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 3))
		|| !TestTrue(TEXT("set-up: five Corrupted Motes"), Bag->AddMaterial(Mote, 5)))
	{
		return false;
	}

	// NOTHING IN THE FIRST TWO SLOTS, where the gem and material stacks sat until #2849.
	TestTrue(TEXT("slot 0 holds nothing to drop"),
			 UCataclysmWearing::DropCarried(Bag, 0, nullptr, FVector::ZeroVector)
				 == ECataclysmWearResult::NothingToDrop);
	TestTrue(TEXT("nor does slot 1"),
			 UCataclysmWearing::DropCarried(Bag, 1, nullptr, FVector::ZeroVector)
				 == ECataclysmWearResult::NothingToDrop);
	TestEqual(TEXT("all three gems are still carried"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 3);
	TestEqual(TEXT("and all five motes"), Bag->CountOfMaterial(Mote), 5);

	// CONTROL: GEAR IN SLOT 0 IS OFFERED TO THE FLOOR, which has no world to put it in, so "nothing to drop" above is
	// about what the slot holds and not about a drop that cannot be reached.
	FCataclysmItem Piece;
	Piece.Base = FName(TEXT("Test_Gem_Stack_Filler"));
	if (!TestEqual(TEXT("set-up: a piece of gear goes in slot 0"), Bag->AddItem(Piece), 0))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: the piece is offered to the floor, which has no world to put it in"),
			 UCataclysmWearing::DropCarried(Bag, 0, nullptr, FVector::ZeroVector)
				 == ECataclysmWearResult::TheFloorRefusedIt);
	TestEqual(TEXT("and the stacks are untouched"),
			  Bag->CountOfGems(Viper, ECataclysmRarity::Superb) + Bag->CountOfMaterial(Mote), 8);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GK4. A gem stack's cell and tooltip: its name, count, rarity's border, rarity and type, effect at that rarity, and
// the socket it fits.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksTooltipTest,
	"Cataclysm.GemStacks.AGemStacksCellAndTooltipNameItsRarityEffectAndSocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStacksTooltipTest::RunTest(const FString&)
{
	using namespace CataclysmGemStacksTest;
	const UDataTable* Gems = UCataclysmGems::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the generated gem table"), Gems))
	{
		return false;
	}
	const FCataclysmCarriedSlot Slot = GemSlot(Viper, ECataclysmRarity::Superb, 3);

	TestEqual(TEXT("the cell names the gem"), UCataclysmInventoryScreen::LabelFor(Slot, nullptr, nullptr),
			  FString(TEXT("Of The Viper")));
	TestEqual(TEXT("and shows how many"), UCataclysmInventoryScreen::QuantityTextFor(Slot), FString(TEXT("3")));
	TestEqual(TEXT("with its rarity's border, as gear of that rarity has"),
			  UCataclysmInventoryScreen::BorderThicknessFor(Slot, nullptr),
			  UCataclysmDropPickup::NameBorderThicknessFor(ECataclysmRarity::Superb));

	const TArray<FString> Lines = UCataclysmItemTooltip::LinesFor(Slot, nullptr, nullptr, nullptr, nullptr, nullptr);
	const TArray<FString> Expected = {TEXT("Of The Viper"), TEXT("Superb Attack gem"), TEXT("60% chance to apply poison"),
									  TEXT("Fits an Attack socket"), TEXT("3 carried")};
	if (TestEqual(TEXT("the tooltip has five lines"), Lines.Num(), Expected.Num()))
	{
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("line %d"), Index + 1), Lines[Index], Expected[Index]);
		}
	}

	// THE EFFECT'S FIGURE IS REPLACED WHEREVER IT SITS, a leading "." included.
	if (const FCataclysmGemRow* Goblin = UCataclysmGems::RowOf(Gems, FName(TEXT("Gem_Of_The_Goblin"))))
	{
		TestEqual(TEXT("Of The Goblin at Everyday"), UCataclysmGems::EffectAt(*Goblin, ECataclysmRarity::Everyday),
				  FString(TEXT("Increases Magic Find by 0.5%")));
	}
	if (const FCataclysmGemRow* Abyss = UCataclysmGems::RowOf(Gems, FName(TEXT("Gem_Of_The_Abyss"))))
	{
		TestEqual(TEXT("Of The Abyss at Cataclysmic"), UCataclysmGems::EffectAt(*Abyss, ECataclysmRarity::Cataclysmic),
				  FString(TEXT("200% chance to apply void splinter")));
	}
	const TArray<FString> Urgency = UCataclysmGems::TooltipLinesFor(FName(TEXT("Gem_Of_Urgency")),
																	ECataclysmRarity::Everyday, 1);
	// OF URGENCY RAISES COOLDOWN REDUCTION, which no class has a base of, so its fourth line says so (GK5).
	TestTrue(TEXT("a Utility gem fits a Utility socket, and one carried says no count"),
			 Urgency.Num() == 5 && Urgency[4] == TEXT("Fits a Utility socket"));
	AddInfo(FString::Printf(TEXT("GEM STACK TOOLTIP: %s"), *FString::Join(Lines, TEXT(" | "))));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GK5. A gem that increases a stat no class has a base of says its increase needs gear, and no other gem does. Ruled
// 2026-11-21, "keep and explain"; the fixed line's form by the coordinating session on 2026-11-22.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksNothingToIncreaseTest,
	"Cataclysm.GemStacks.AGemRaisingAStatNoClassHasSaysItNeedsGearAndOthersDoNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Of The Wraith (evasion) carries the line after its effect. Of The Viper, a flat poison chance, does not; nor does Of
 * The Scholar, an increase of skill experience gain, whose base of 100 the game supplies: the line follows the stat,
 * not the bucket. Prints "NOTHING TO INCREASE".
 */
bool FCataclysmGemStacksNothingToIncreaseTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("set-up: the generated gem table"), UCataclysmGems::LoadGeneratedTable()))
	{
		return false;
	}
	const TArray<FString> Wraith = UCataclysmGems::TooltipLinesFor(FName(TEXT("Gem_Of_The_Wraith")),
																   ECataclysmRarity::Everyday, 1);
	const TArray<FString> Viper = UCataclysmGems::TooltipLinesFor(FName(TEXT("Gem_Of_The_Viper")),
																  ECataclysmRarity::Everyday, 1);
	const TArray<FString> Scholar = UCataclysmGems::TooltipLinesFor(FName(TEXT("Gem_Of_The_Scholar")),
																	ECataclysmRarity::Everyday, 1);
	const FString Line = TEXT("An increase multiplies what you already have. No class has a base evasion, so this gem "
							  "does nothing until your gear gives some.");
	auto Explains = [](const TArray<FString>& Lines)
	{
		return Lines.ContainsByPredicate([](const FString& Text) { return Text.Contains(TEXT("No class has a base")); });
	};
	AddInfo(FString::Printf(TEXT("NOTHING TO INCREASE: Of The Wraith %s | Of The Viper %s | Of The Scholar %s"),
							*FString::Join(Wraith, TEXT(" / ")), *FString::Join(Viper, TEXT(" / ")),
							*FString::Join(Scholar, TEXT(" / "))));

	TestTrue(TEXT("Of The Wraith's fourth line says evasion needs gear"), Wraith.Num() >= 4 && Wraith[3] == Line);
	TestFalse(TEXT("Of The Viper, a flat chance, says nothing of it"), Explains(Viper));
	TestFalse(TEXT("nor does Of The Scholar, an increase of a stat the game gives a base"), Explains(Scholar));
	TestTrue(TEXT("CONTROL: Of The Scholar's tooltip was read"), Scholar.Num() >= 3);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GK6. A carried gem stack survives a save and a load: the bag gathered into a character record, written to JSON as
// the save file is, read back and restored into a fresh bag. Promised by G2's entry; ruled by the coordinating session
// on 2026-11-22 to ride in the empire tree slice 9 window.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksSaveTest,
	"Cataclysm.GemStacks.ACarriedGemStackSurvivesASaveAndALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three Of The Viper at Superb and one Of Rot at Quality, beside a stack of Corrupted Motes, are gathered into a
 * character record, written out and read back, and restored into an empty bag: each stack comes back in the gems or
 * materials tab at its own rarity and count, in the gems tab's order, and a Viper at another rarity is not invented.
 * Since issue #2849 the stacks are saved in `CarriedGems` and `CarriedMaterials`, not in a carried slot. Prints "GEM
 * STACK SAVED".
 */
bool FCataclysmGemStacksSaveTest::RunTest(const FString&)
{
	using namespace CataclysmGemStacksTest;
	UCataclysmInventoryComponent* Bag = NewBag();
	if (!TestTrue(TEXT("set-up: three stacks are taken"),
				  Bag->AddGems(Viper, ECataclysmRarity::Superb, 3) && Bag->AddGems(Rot, ECataclysmRarity::Quality, 1)
					  && Bag->AddMaterial(Mote, 4))
		|| !TestEqual(TEXT("set-up: two of them gems"), Bag->GetGemStacks().Num(), 2))
	{
		return false;
	}

	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>(GetTransientPackage());
	Record->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
	FCataclysmSaveGather::CarriedSlotsFrom(*Bag, Record->CarriedSlots);
	FCataclysmSaveGather::CarriedStacksFrom(*Bag, Record->CarriedMaterials, Record->CarriedGems);
	FString Text;
	FString Error;
	if (!TestTrue(TEXT("set-up: the record is written"), FCataclysmSaveStorage::ToJson(Record, Text, Error)))
	{
		AddError(Error);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (!TestNotNull(TEXT("set-up: the record is read back"), Read))
	{
		AddError(Message);
		return false;
	}

	// THE STORES FIRST, THEN THE SLOTS, the order the load uses (RestoreSlots moves any stack it finds onto a store).
	UCataclysmInventoryComponent* Loaded = NewBag();
	Loaded->RestoreStacks(Read->CarriedMaterials, Read->CarriedGems);
	const bool bRestored = Loaded->RestoreSlots(Read->CarriedSlots);
	const TArray<FCataclysmCarriedSlot>& Gems = Loaded->GetGemStacks();
	AddInfo(FString::Printf(TEXT("GEM STACK SAVED: restored %d; %d gem stack(s) in the file, %d restored, first %s; Viper "
								 "Superb %d, Rot Quality %d, motes %d; Viper Quality %d; %d gear slot(s) used"),
							bRestored, Read->CarriedGems.Num(), Gems.Num(),
							Gems.IsEmpty() ? TEXT("none") : *Gems[0].Gem.ToString(),
							Loaded->CountOfGems(Viper, ECataclysmRarity::Superb),
							Loaded->CountOfGems(Rot, ECataclysmRarity::Quality), Loaded->CountOfMaterial(Mote),
							Loaded->CountOfGems(Viper, ECataclysmRarity::Quality), Loaded->NumItems()));

	TestTrue(TEXT("the bag is restored"), bRestored);
	TestTrue(TEXT("and the file names the gem"), Text.Contains(TEXT("\"Gem_Of_The_Viper\"")));
	TestEqual(TEXT("the file holds two gem stacks"), Read->CarriedGems.Num(), 2);
	TestEqual(TEXT("and one material stack"), Read->CarriedMaterials.Num(), 1);
	TestEqual(TEXT("Of The Viper comes back, 3 of them at Superb"),
			  Loaded->CountOfGems(Viper, ECataclysmRarity::Superb), 3);
	TestEqual(TEXT("Of Rot comes back, 1 at Quality"), Loaded->CountOfGems(Rot, ECataclysmRarity::Quality), 1);
	// BY GEM NAME, THEN RARITY: "Gem_Of_Rot" before "Gem_Of_The_Viper", as the gems tab draws them.
	TestTrue(TEXT("in the gems tab's order, Of Rot first"),
			 Gems.Num() == 2 && Gems[0].Gem == Rot && Gems[1].Gem == Viper);
	TestEqual(TEXT("no Viper at another rarity is invented"), Loaded->CountOfGems(Viper, ECataclysmRarity::Quality), 0);
	TestEqual(TEXT("CONTROL: the motes beside them come back too"), Loaded->CountOfMaterial(Mote), 4);
	TestEqual(TEXT("and no gear slot holds any of it"), Loaded->NumItems(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
