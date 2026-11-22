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
#include "UObject/Package.h"

/**
 * GEMS AS ITEMS: CARRIED IN STACKS, ONE PER GEM AND RARITY. Issue #46, slice G2.
 *
 * Ruled by the coordinating session on 2026-11-21: gems stack, one stack per gem and rarity, for the reason crafting
 * materials stack. A gem stack shows its name, its count, its rarity's colour and border, and a tooltip naming its
 * rarity and type, its effect at that rarity and the socket it fits. A dropped stack is offered to the floor whole, as
 * one gem drop (slice G5; until it, G2 refused to drop gems at all).
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

	TestEqual(TEXT("two Superb Of The Viper go in the first slot"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 2), 0);
	TestEqual(TEXT("a third joins their stack"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 1), 0);
	TestEqual(TEXT("so three are carried"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 3);
	TestEqual(TEXT("an Everyday Of The Viper is a different stack"), Bag->AddGems(Viper, ECataclysmRarity::Everyday, 1),
			  1);
	TestEqual(TEXT("and does not join the Superb one"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 3);
	TestEqual(TEXT("a material takes the next slot"), Bag->AddMaterial(Mote, 5), 2);
	TestEqual(TEXT("three slots are used"), Bag->NumItems(), 3);
	TestFalse(TEXT("a gem stack is not an empty slot"), UCataclysmInventoryComponent::SlotIsEmpty(Bag->GetSlots()[0]));
	TestNull(TEXT("and holds no item"), Bag->ItemAt(0));
	TestEqual(TEXT("the material is counted apart from the gems"), Bag->CountOfMaterial(Mote), 5);

	TestEqual(TEXT("CONTROL: a gem with no name is refused"), Bag->AddGems(NAME_None, ECataclysmRarity::Superb, 1),
			  INDEX_NONE);
	TestEqual(TEXT("CONTROL: none of a gem is refused"), Bag->AddGems(Rot, ECataclysmRarity::Superb, 0), INDEX_NONE);
	TestEqual(TEXT("and nothing more was stored"), Bag->NumItems(), 3);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GK2. A full bag refuses a gem nothing carries yet, and still adds to a stack it carries.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksFullBagTest,
	"Cataclysm.GemStacks.AFullBagRefusesANewGemAndStillStacksACarriedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStacksFullBagTest::RunTest(const FString&)
{
	using namespace CataclysmGemStacksTest;
	UCataclysmInventoryComponent* Bag = NewBag();
	if (!TestNotNull(TEXT("set-up: a bag"), Bag)
		|| !TestEqual(TEXT("set-up: one Superb Of The Viper"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 1), 0))
	{
		return false;
	}
	FCataclysmItem Piece;
	Piece.Base = FName(TEXT("Test_Gem_Stack_Filler"));
	for (int32 Index = 1; Index < UCataclysmInventoryComponent::SlotCount; ++Index)
	{
		Bag->AddItem(Piece);
	}
	if (!TestTrue(TEXT("set-up: the bag is full"), Bag->IsFull()))
	{
		return false;
	}
	TestEqual(TEXT("a gem nothing carries is refused"), Bag->AddGems(Rot, ECataclysmRarity::Everyday, 1), INDEX_NONE);
	TestEqual(TEXT("and none of it was stored"), Bag->CountOfGems(Rot, ECataclysmRarity::Everyday), 0);
	TestEqual(TEXT("a carried stack still has room"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 4), 0);
	TestEqual(TEXT("so five are carried"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 5);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GK3. A gem stack is offered to the floor, as a material is, and stays in the bag when the floor refuses it. Slice G5
// lifted G2's refusal; `Cataclysm.GemDrops.AGemDropIsSavedAndRestoredAndADroppedStackLiesWholeOnTheFloor` drops one
// into a world.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStacksDropTest,
	"Cataclysm.GemStacks.AGemStackIsOfferedToTheFloorAndStaysInTheBagWhenItRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStacksDropTest::RunTest(const FString&)
{
	using namespace CataclysmGemStacksTest;
	UCataclysmInventoryComponent* Bag = NewBag();
	if (!TestNotNull(TEXT("set-up: a bag"), Bag)
		|| !TestEqual(TEXT("set-up: three Superb Of The Viper"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 3), 0)
		|| !TestEqual(TEXT("set-up: five Corrupted Motes"), Bag->AddMaterial(Mote, 5), 1))
	{
		return false;
	}

	// NO WORLD: the gems are offered to the floor, which has nowhere to put them, so nothing leaves the bag.
	TestTrue(TEXT("the gems are offered to the floor, which has no world to put them in"),
			 UCataclysmWearing::DropCarried(Bag, 0, nullptr, FVector::ZeroVector)
				 == ECataclysmWearResult::TheFloorRefusedIt);
	TestEqual(TEXT("and all three are still carried"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 3);

	// CONTROL: A MATERIAL IS OFFERED THE SAME WAY, and refused for the same reason.
	TestTrue(TEXT("CONTROL: a material is offered to the floor, which has no world to put it in"),
			 UCataclysmWearing::DropCarried(Bag, 1, nullptr, FVector::ZeroVector)
				 == ECataclysmWearResult::TheFloorRefusedIt);
	TestEqual(TEXT("and it is still carried too"), Bag->CountOfMaterial(Mote), 5);
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
	TestTrue(TEXT("a Utility gem fits a Utility socket, and one carried says no count"),
			 Urgency.Num() == 4 && Urgency[3] == TEXT("Fits a Utility socket"));
	AddInfo(FString::Printf(TEXT("GEM STACK TOOLTIP: %s"), *FString::Join(Lines, TEXT(" | "))));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
