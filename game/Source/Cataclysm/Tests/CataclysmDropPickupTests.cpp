// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for picking a drop up. Issue #707.
 *
 * THE THREE JUDGEMENTS, AND WHY THEY ARE NOT IN THE HEADS-UP DISPLAY OR THE
 * CONTROLLER. Which name the cursor is over, whether the character is near
 * enough, and what moving the item actually does are all on
 * UCataclysmDropPickup as statics. The automation command runs with -nullrhi
 * and AHUD::PostRender checks FApp::CanEverRender() before calling DrawHUD, so
 * nothing inside a draw call can be tested at all; and nothing here possesses a
 * pawn or presses a mouse button, so the input handler cannot be either. Keeping
 * the judgements out of both leaves all three covered.
 *
 * WHAT IS THEREFORE NOT COVERED, said plainly: that the heads-up display records
 * the rectangle it drew, that ACataclysmPlayerController asks it, and that a
 * real left click reaches any of this. Those were checked by playing.
 */
namespace CataclysmDropPickupTest
{
	using FPickup = UCataclysmDropPickup;

	/** A whole enough item to be stored: it has a base, so it is not empty. */
	FCataclysmItem SomeItem(const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = 3;
		Item.Sockets = 1;
		Item.Residue = 42.0f;
		return Item;
	}

	/** A drop lying at a place, carrying an item and a name. */
	ACataclysmDroppedItem* DropAt(UWorld* World, const FVector& Where,
								  const TCHAR* Base, const TCHAR* Name)
	{
		ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(
			Where, FRotator::ZeroRotator);
		if (Drop)
		{
			Drop->Item = SomeItem(Base);
			Drop->DisplayName = Name;
		}
		return Drop;
	}

	/** A rectangle, given its corners. Not named Rect: SlateRenderer.h declares
	 *  a struct by that name and the two are ambiguous here. */
	FBox2D MakeRect(float Left, float Top, float Right, float Bottom)
	{
		return FBox2D(FVector2D(Left, Top), FVector2D(Right, Bottom));
	}
}

// ---------------------------------------------------------------------------
// How near is near enough
// ---------------------------------------------------------------------------

/**
 * The range is three metres and it is measured flat.
 *
 * THE HEIGHT CASE IS THE ONE WORTH HAVING. A drop is spawned at the height of
 * the corpse that produced it and nothing traces it down to the floor, which is
 * issue #690. A three-dimensional distance would make a drop from a tall
 * creature quietly harder to reach than the same drop on flat ground, and the
 * player would have no way to tell why.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPickupRangeTest,
	"Cataclysm.DropPickup.TheRangeIsThreeMetresMeasuredFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPickupRangeTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	TestEqual(TEXT("three metres, from Diablo's three yards"),
		FPickup::PickupRangeCm, 300.0f);

	const FVector Standing(0.0f, 0.0f, 0.0f);

	TestTrue(TEXT("underfoot is in reach"),
		FPickup::IsWithinPickupRange(Standing, FVector(0.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("two metres away is in reach"),
		FPickup::IsWithinPickupRange(Standing, FVector(200.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("just inside three metres is in reach"),
		FPickup::IsWithinPickupRange(Standing, FVector(299.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("exactly three metres is in reach"),
		FPickup::IsWithinPickupRange(Standing, FVector(300.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("just outside three metres is not"),
		FPickup::IsWithinPickupRange(Standing, FVector(301.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("ten metres away is not"),
		FPickup::IsWithinPickupRange(Standing, FVector(1000.0f, 0.0f, 0.0f)));

	// DIAGONAL, so the test cannot pass by comparing one axis. 212 on each of
	// two axes is 299.8 apart; 213 on each is 301.2.
	TestTrue(TEXT("299.8 cm diagonally is in reach"),
		FPickup::IsWithinPickupRange(Standing, FVector(212.0f, 212.0f, 0.0f)));
	TestFalse(TEXT("301.2 cm diagonally is not"),
		FPickup::IsWithinPickupRange(Standing, FVector(213.0f, 213.0f, 0.0f)));

	// HEIGHT IS IGNORED. Two metres away and four metres up is 4.47 m apart in
	// three dimensions, so a 3D test would refuse it. It is in reach.
	TestTrue(TEXT("a drop four metres overhead is still in reach"),
		FPickup::IsWithinPickupRange(Standing, FVector(200.0f, 0.0f, 400.0f)));
	TestTrue(TEXT("and one four metres below is too"),
		FPickup::IsWithinPickupRange(Standing, FVector(200.0f, 0.0f, -400.0f)));

	// AND HEIGHT DOES NOT RESCUE SOMETHING OUT OF REACH FLAT.
	TestFalse(TEXT("ten metres away at the same height is still out of reach"),
		FPickup::IsWithinPickupRange(Standing, FVector(1000.0f, 0.0f, 10.0f)));

	// IT DOES NOT MATTER WHICH WAY ROUND THE TWO ARE GIVEN.
	TestTrue(TEXT("the measure is symmetric"),
		FPickup::IsWithinPickupRange(FVector(500.0f, 500.0f, 0.0f),
									 FVector(600.0f, 500.0f, 0.0f)));

	return true;
}

// ---------------------------------------------------------------------------
// Which name the cursor is over
// ---------------------------------------------------------------------------

/**
 * The name on top is the one clicked, and a click on nothing finds nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPickupHitTest,
	"Cataclysm.DropPickup.TheNameOnTopIsTheOneClicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPickupHitTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	// NOTHING DRAWN. Every automation run is in this state, and so is a frame
	// with no drops on screen.
	const TArray<FBox2D> Nothing;
	TestEqual(TEXT("nothing drawn means nothing clicked"),
		FPickup::IndexOfNameUnderPoint(Nothing, FVector2D(100.0f, 100.0f)),
		INDEX_NONE);

	TArray<FBox2D> Rects;
	Rects.Add(MakeRect(100.0f, 100.0f, 200.0f, 120.0f));	// 0
	Rects.Add(MakeRect(400.0f, 300.0f, 560.0f, 320.0f));	// 1

	TestEqual(TEXT("a point in the first name finds it"),
		FPickup::IndexOfNameUnderPoint(Rects, FVector2D(150.0f, 110.0f)), 0);
	TestEqual(TEXT("a point in the second finds that one"),
		FPickup::IndexOfNameUnderPoint(Rects, FVector2D(500.0f, 310.0f)), 1);

	// THE EDGES COUNT. A name is a small target and half a pixel of it is still
	// the player pointing at it.
	TestEqual(TEXT("the top left corner counts"),
		FPickup::IndexOfNameUnderPoint(Rects, FVector2D(100.0f, 100.0f)), 0);
	TestEqual(TEXT("the bottom right corner counts"),
		FPickup::IndexOfNameUnderPoint(Rects, FVector2D(200.0f, 120.0f)), 0);

	TestEqual(TEXT("a point outside every name finds none"),
		FPickup::IndexOfNameUnderPoint(Rects, FVector2D(300.0f, 200.0f)),
		INDEX_NONE);
	TestEqual(TEXT("a point just past a name's right edge finds none"),
		FPickup::IndexOfNameUnderPoint(Rects, FVector2D(201.0f, 110.0f)),
		INDEX_NONE);
	TestEqual(TEXT("and one just above its top edge finds none"),
		FPickup::IndexOfNameUnderPoint(Rects, FVector2D(150.0f, 99.0f)),
		INDEX_NONE);

	// TWO NAMES OVERLAPPING. Two drops from one kill land 25 cm apart, so this
	// is ordinary rather than a corner case. The later one is drawn on top and
	// is the one the player can read, so it is the one they mean.
	TArray<FBox2D> Stacked;
	Stacked.Add(MakeRect(100.0f, 100.0f, 300.0f, 120.0f));	// underneath
	Stacked.Add(MakeRect(150.0f, 105.0f, 350.0f, 125.0f));	// on top
	TestEqual(TEXT("where they overlap, the one on top wins"),
		FPickup::IndexOfNameUnderPoint(Stacked, FVector2D(200.0f, 110.0f)), 1);
	TestEqual(TEXT("where only the lower one is, it still answers"),
		FPickup::IndexOfNameUnderPoint(Stacked, FVector2D(110.0f, 110.0f)), 0);
	TestEqual(TEXT("and where only the upper one is, it answers"),
		FPickup::IndexOfNameUnderPoint(Stacked, FVector2D(340.0f, 122.0f)), 1);

	// AN UNSET RECTANGLE IS NOT A TARGET. A default FBox2D has bIsValid false
	// and a zero extent at the origin, so a click at (0,0) would otherwise hit
	// every drop that failed to project.
	TArray<FBox2D> WithAnUnsetOne;
	WithAnUnsetOne.Add(FBox2D());
	TestEqual(TEXT("an unset rectangle is never under the cursor"),
		FPickup::IndexOfNameUnderPoint(WithAnUnsetOne, FVector2D(0.0f, 0.0f)),
		INDEX_NONE);

	return true;
}

// ---------------------------------------------------------------------------
// What picking up actually does
// ---------------------------------------------------------------------------

/**
 * Taking a drop moves the whole item into a slot and removes it from the floor.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPickupTakesTheItemTest,
	"Cataclysm.DropPickup.TakingADropMovesTheItemAndClearsTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPickupTakesTheItemTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmInventoryComponent* Inventory =
		NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	ACataclysmDroppedItem* Drop = DropAt(World, FVector(100.0f, 0.0f, 0.0f),
										 TEXT("Greataxe"), TEXT("Superb Greataxe"));
	if (!TestNotNull(TEXT("inventory"), Inventory)
		|| !TestNotNull(TEXT("drop"), Drop))
	{
		return false;
	}

	TestTrue(TEXT("the item was taken"), FPickup::TakeInto(Inventory, Drop));

	TestEqual(TEXT("it is in the inventory"), Inventory->NumItems(), 1);
	const FCataclysmItem* Carried = Inventory->ItemAt(0);
	if (!TestNotNull(TEXT("in the first slot"), Carried))
	{
		return false;
	}

	// THE WHOLE ITEM, not just its name. A pick-up that kept only the base would
	// silently strip every roll off everything the player collected.
	TestEqual(TEXT("the base came with it"), Carried->Base, FName(TEXT("Greataxe")));
	TestEqual(TEXT("the upgrade level came with it"), Carried->GearLevel, 3);
	TestEqual(TEXT("the sockets came with it"), Carried->Sockets, 1);
	TestEqual(TEXT("the residue came with it"), Carried->Residue, 42.0f);

	// AND THE FLOOR IS CLEAR. Counting actors rather than asking the pointer,
	// because Destroy marks an actor and the heads-up display iterates the
	// world; a drop still in that iteration would go on being drawn.
	int32 StillOnTheFloor = 0;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			++StillOnTheFloor;
		}
	}
	TestEqual(TEXT("nothing is left lying there"), StillOnTheFloor, 0);

	// NOTHING TO TAKE TWICE. A second click on the same drop, which is what a
	// stale rectangle from the last frame would produce, does nothing.
	TestFalse(TEXT("a destroyed drop cannot be taken again"),
		FPickup::TakeInto(Inventory, Drop));
	TestEqual(TEXT("and nothing was duplicated"), Inventory->NumItems(), 1);

	return true;
}

/**
 * A full inventory leaves the drop where it is, whole.
 *
 * THIS IS THE DESIGN'S OWN RULE and it is the case that would be easiest to get
 * wrong in a way nobody noticed: destroying the actor first and then failing to
 * store the item loses it, and the player would see a click that made an item
 * vanish. The decision of 2026-08-14 says a player cannot leave a dungeon
 * partway through, so what will not fit stays on the floor to choose about.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPickupFullInventoryTest,
	"Cataclysm.DropPickup.AFullInventoryLeavesTheDropOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPickupFullInventoryTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmInventoryComponent* Inventory =
		NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	for (int32 N = 0; N < UCataclysmInventoryComponent::SlotCount; ++N)
	{
		Inventory->AddItem(SomeItem(*FString::Printf(TEXT("Filler%d"), N)));
	}
	if (!TestTrue(TEXT("the inventory is full"), Inventory->IsFull()))
	{
		return false;
	}

	ACataclysmDroppedItem* Drop = DropAt(World, FVector(100.0f, 0.0f, 0.0f),
										 TEXT("Cataclysmic Greataxe"),
										 TEXT("Cataclysmic Greataxe of Ruin"));
	if (!TestNotNull(TEXT("drop"), Drop))
	{
		return false;
	}

	TestFalse(TEXT("it cannot be taken"), FPickup::TakeInto(Inventory, Drop));

	// THE ITEM IS NOT DESTROYED AND NOT STORED. Both halves matter.
	TestTrue(TEXT("the drop is still lying there"), IsValid(Drop));
	TestEqual(TEXT("still carrying its item"), Drop->Item.Base,
		FName(TEXT("Cataclysmic Greataxe")));
	TestEqual(TEXT("still named"), Drop->DisplayName,
		FString(TEXT("Cataclysmic Greataxe of Ruin")));
	TestEqual(TEXT("and the inventory did not grow"), Inventory->NumItems(),
		UCataclysmInventoryComponent::SlotCount);

	// AND IT CAN BE TAKEN ONCE THERE IS ROOM, so the refusal is about space and
	// not about the drop having been spoiled by the attempt.
	TestTrue(TEXT("a slot is freed"), Inventory->RemoveItemAt(10));
	TestTrue(TEXT("now it can be taken"), FPickup::TakeInto(Inventory, Drop));
	TestEqual(TEXT("and it went into the freed slot"),
		Inventory->ItemAt(10) ? Inventory->ItemAt(10)->Base : FName(),
		FName(TEXT("Cataclysmic Greataxe")));

	return true;
}

/**
 * Taking nothing, or taking into nothing, is refused rather than crashing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPickupNothingTest,
	"Cataclysm.DropPickup.TakingNothingIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPickupNothingTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmInventoryComponent* Inventory =
		NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	ACataclysmDroppedItem* Drop = DropAt(World, FVector::ZeroVector,
										 TEXT("Helm"), TEXT("Quality Helm"));
	if (!TestNotNull(TEXT("inventory"), Inventory)
		|| !TestNotNull(TEXT("drop"), Drop))
	{
		return false;
	}

	TestFalse(TEXT("no drop to take"), FPickup::TakeInto(Inventory, nullptr));
	TestFalse(TEXT("nowhere to put it"), FPickup::TakeInto(nullptr, Drop));

	// AND THE DROP SURVIVED BOTH. A pawn with no inventory clicking a drop must
	// not destroy it.
	TestTrue(TEXT("the drop is untouched"), IsValid(Drop));
	TestEqual(TEXT("and nothing was carried"), Inventory->NumItems(), 0);

	// A DROP CARRYING NO ITEM IS REFUSED BY THE INVENTORY, so a malformed roll
	// cannot occupy a slot. The drop stays, which is visible rather than silent.
	ACataclysmDroppedItem* Empty = World->SpawnActor<ACataclysmDroppedItem>(
		FVector(50.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an empty drop"), Empty))
	{
		return false;
	}
	TestFalse(TEXT("a drop with no item cannot be taken"),
		FPickup::TakeInto(Inventory, Empty));
	TestTrue(TEXT("and it is still there"), IsValid(Empty));
	TestEqual(TEXT("and nothing was carried"), Inventory->NumItems(), 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
