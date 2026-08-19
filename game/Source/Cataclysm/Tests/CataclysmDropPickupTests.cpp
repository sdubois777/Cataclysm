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


// ---------------------------------------------------------------------------
// Two names never printed on top of each other
// ---------------------------------------------------------------------------

namespace CataclysmDropPickupTest
{
	/** Whether two rectangles share any area at all. */
	bool Overlaps(const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X < B.Max.X && B.Min.X < A.Max.X
			&& A.Min.Y < B.Max.Y && B.Min.Y < A.Max.Y;
	}

	/** Whether any two of a set of rectangles share area. */
	bool AnyOverlap(const TArray<FBox2D>& Rects)
	{
		for (int32 A = 0; A < Rects.Num(); ++A)
		{
			for (int32 B = A + 1; B < Rects.Num(); ++B)
			{
				if (Rects[A].bIsValid && Rects[B].bIsValid
					&& Overlaps(Rects[A], Rects[B]))
				{
					return true;
				}
			}
		}
		return false;
	}
}

/**
 * Names that would print over each other are moved apart, and the rest are not.
 *
 * WHY THIS IS NEEDED WHEN THE DROPS ARE ALREADY SPREAD OUT ON THE GROUND. The
 * scatter puts several drops from one kill around a circle, which separates them
 * in the world. The camera looks down at that circle, so two drops on opposite
 * sides of it can land at nearly the same height on screen, and an item name is
 * far wider than it is tall. The project owner saw the names printed over each
 * other on 2026-08-19.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNameSeparationTest,
	"Cataclysm.DropPickup.NamesThatWouldOverlapAreMovedApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNameSeparationTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	constexpr float Gap = 4.0f;

	// NOTHING TO DO. Two names far apart are left exactly where they were, so
	// the common case costs nothing and nothing drifts for no reason.
	TArray<FBox2D> Apart;
	Apart.Add(MakeRect(100.0f, 100.0f, 200.0f, 120.0f));
	Apart.Add(MakeRect(100.0f, 400.0f, 200.0f, 420.0f));
	const TArray<FBox2D> Untouched = Apart;

	FPickup::SeparateOverlappingNames(Apart, Gap);
	TestEqual(TEXT("the first name did not move"), Apart[0], Untouched[0]);
	TestEqual(TEXT("nor did the second"), Apart[1], Untouched[1]);

	// SIDE BY SIDE IS NOT OVERLAPPING. Two names at the same height that do not
	// share any columns are both readable already.
	TArray<FBox2D> Beside;
	Beside.Add(MakeRect(100.0f, 100.0f, 200.0f, 120.0f));
	Beside.Add(MakeRect(400.0f, 100.0f, 500.0f, 120.0f));
	const TArray<FBox2D> BesideBefore = Beside;

	FPickup::SeparateOverlappingNames(Beside, Gap);
	TestEqual(TEXT("a name beside another does not move"), Beside[1],
		BesideBefore[1]);

	// EXACTLY ON TOP OF EACH OTHER, which is what the project owner saw.
	TArray<FBox2D> Stacked;
	Stacked.Add(MakeRect(100.0f, 100.0f, 300.0f, 120.0f));
	Stacked.Add(MakeRect(100.0f, 100.0f, 300.0f, 120.0f));
	Stacked.Add(MakeRect(100.0f, 100.0f, 300.0f, 120.0f));

	FPickup::SeparateOverlappingNames(Stacked, Gap);
	TestFalse(TEXT("three names on one spot no longer overlap"),
		AnyOverlap(Stacked));

	// THE HIGHEST ONE STAYS PUT and the others go below it, in order.
	TestEqual(TEXT("the first keeps its place"), Stacked[0].Min.Y, 100.0);
	TestEqual(TEXT("the second sits under it, a gap away"),
		Stacked[1].Min.Y, 124.0);
	TestEqual(TEXT("and the third under that"), Stacked[2].Min.Y, 148.0);

	// AND THEY KEEP THEIR SIZE. A name squashed to fit would be unreadable in a
	// different way.
	for (const FBox2D& Rect : Stacked)
	{
		TestEqual(TEXT("the name is still 20 pixels tall"),
			Rect.Max.Y - Rect.Min.Y, 20.0);
		TestEqual(TEXT("and still 200 wide"), Rect.Max.X - Rect.Min.X, 200.0);
	}

	// NOTHING MOVES SIDEWAYS. A name that drifted left or right would no longer
	// point at the item it belongs to.
	for (const FBox2D& Rect : Stacked)
	{
		TestEqual(TEXT("the left edge is where it was"), Rect.Min.X, 100.0);
	}

	return true;
}

/**
 * A crowd of names is separated whatever order they arrive in.
 *
 * PARTLY OVERLAPPING RATHER THAN IDENTICAL, because that is the real case: five
 * drops from one kill land in a circle and their names land in a rough band.
 * Moving one clear of its neighbour can push it onto the next, which is why the
 * pass repeats rather than making one comparison each.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNameSeparationCrowdTest,
	"Cataclysm.DropPickup.ACrowdOfNamesEndsUpWithNoneOverlapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNameSeparationCrowdTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	constexpr float Gap = 4.0f;

	// TWELVE, WHICH IS WHAT A CATACLYSM BOSS DROPS. Each one five pixels below
	// the last and twenty tall, so every one overlaps its neighbours.
	TArray<FBox2D> Crowd;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Top = 200.0f + static_cast<float>(Index) * 5.0f;
		Crowd.Add(MakeRect(300.0f, Top, 520.0f, Top + 20.0f));
	}

	FPickup::SeparateOverlappingNames(Crowd, Gap);
	TestFalse(TEXT("twelve names no longer overlap"), AnyOverlap(Crowd));

	// GIVEN IN THE OPPOSITE ORDER, the answer is the same set of positions. The
	// world hands drops over in no particular order, so the layout must not
	// depend on it.
	TArray<FBox2D> Reversed;
	for (int32 Index = 11; Index >= 0; --Index)
	{
		const float Top = 200.0f + static_cast<float>(Index) * 5.0f;
		Reversed.Add(MakeRect(300.0f, Top, 520.0f, Top + 20.0f));
	}

	FPickup::SeparateOverlappingNames(Reversed, Gap);
	TestFalse(TEXT("and still do not overlap given in reverse"),
		AnyOverlap(Reversed));

	TArray<double> Forward;
	for (const FBox2D& Rect : Crowd) { Forward.Add(Rect.Min.Y); }
	TArray<double> Backward;
	for (const FBox2D& Rect : Reversed) { Backward.Add(Rect.Min.Y); }
	Forward.Sort();
	Backward.Sort();

	if (TestEqual(TEXT("the same number of names"), Forward.Num(),
				  Backward.Num()))
	{
		for (int32 Index = 0; Index < Forward.Num(); ++Index)
		{
			TestEqual(TEXT("the same set of heights, whatever the input order"),
				Forward[Index], Backward[Index]);
		}
	}

	// AN UNSET RECTANGLE TAKES NO PART. A drop that failed to project has no
	// place on screen, and it must not push a real name out of the way.
	TArray<FBox2D> WithAnUnsetOne;
	WithAnUnsetOne.Add(FBox2D());
	WithAnUnsetOne.Add(MakeRect(0.0f, 0.0f, 100.0f, 20.0f));
	FPickup::SeparateOverlappingNames(WithAnUnsetOne, Gap);
	TestEqual(TEXT("a real name at the origin is not pushed by an unset one"),
		WithAnUnsetOne[1].Min.Y, 0.0);

	// AND NEITHER AN EMPTY LIST NOR A SINGLE NAME IS A PROBLEM.
	TArray<FBox2D> Empty;
	FPickup::SeparateOverlappingNames(Empty, Gap);
	TestEqual(TEXT("an empty list stays empty"), Empty.Num(), 0);

	TArray<FBox2D> Alone;
	Alone.Add(MakeRect(10.0f, 10.0f, 60.0f, 30.0f));
	FPickup::SeparateOverlappingNames(Alone, Gap);
	TestEqual(TEXT("a lone name does not move"), Alone[0].Min.Y, 10.0);

	return true;
}


// ---------------------------------------------------------------------------
// The border that carries a rarity without using colour
// ---------------------------------------------------------------------------

/**
 * Every rarity gets its own border thickness, thinnest at the bottom.
 *
 * WHAT THIS IS FOR. The Interface Colour section of docs/Cataclysm_GDD_v2.md
 * requires the drop marker to "differ by shape or motion as well as by colour",
 * because a player who cannot separate two hues still has to separate two
 * rarities. About 8% of men have red-green colour blindness and the ramp puts
 * green, yellow, orange and red on four adjacent rungs. Issue #718.
 *
 * TWO RARITIES SHARING A THICKNESS WOULD BE THE FAULT, in the same way two
 * sharing a colour would be, so that is what this checks rather than the
 * individual numbers.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNameBorderTest,
	"Cataclysm.DropPickup.EveryRarityGetsItsOwnBorderThickness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNameBorderTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	const ECataclysmRarity Ladder[] = {
		ECataclysmRarity::Everyday,	  ECataclysmRarity::Quality,
		ECataclysmRarity::Superb,	  ECataclysmRarity::Masterful,
		ECataclysmRarity::Legendary,  ECataclysmRarity::Mythical,
		ECataclysmRarity::Ascendant,  ECataclysmRarity::Cataclysmic,
	};

	TestEqual(TEXT("the thinnest border is one pixel"),
		FPickup::ThinnestNameBorderPx, 1);
	TestEqual(TEXT("Everyday gets the thinnest"),
		FPickup::NameBorderThicknessFor(ECataclysmRarity::Everyday), 1);
	TestEqual(TEXT("and Cataclysmic gets eight"),
		FPickup::NameBorderThicknessFor(ECataclysmRarity::Cataclysmic), 8);

	// ONE THICKER A RUNG, WITH NO TWO THE SAME. Checked as a sequence rather
	// than as eight separate numbers, because sharing a thickness is the fault
	// and a list of literals would not say so.
	int32 Previous = 0;
	for (ECataclysmRarity Rarity : Ladder)
	{
		const int32 Thickness = FPickup::NameBorderThicknessFor(Rarity);

		TestTrue(*FString::Printf(
				TEXT("rarity %d is thicker than the rung below it (%d after %d)"),
				static_cast<int32>(Rarity), Thickness, Previous),
			Thickness > Previous);
		Previous = Thickness;
	}

	return true;
}

/**
 * The tag a name occupies grows with its rarity, on every side, and the text
 * stays where it was measured.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNameTagTest,
	"Cataclysm.DropPickup.TheTagGrowsAroundTheTextByItsBorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNameTagTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	const FBox2D Text = MakeRect(100.0f, 200.0f, 300.0f, 220.0f);

	// EVERYDAY: three pixels of padding and one of border, so four on each side.
	const FBox2D Thin = FPickup::TagAround(
		Text, FPickup::NameBorderThicknessFor(ECataclysmRarity::Everyday));
	TestEqual(TEXT("an Everyday tag starts four pixels left of its text"),
		Thin.Min.X, 96.0);
	TestEqual(TEXT("and four above it"), Thin.Min.Y, 196.0);
	TestEqual(TEXT("and four right of it"), Thin.Max.X, 304.0);
	TestEqual(TEXT("and four below it"), Thin.Max.Y, 224.0);

	// CATACLYSMIC: three of padding and eight of border, so eleven a side.
	const FBox2D Thick = FPickup::TagAround(
		Text, FPickup::NameBorderThicknessFor(ECataclysmRarity::Cataclysmic));
	TestEqual(TEXT("a Cataclysmic tag starts eleven pixels left of its text"),
		Thick.Min.X, 89.0);
	TestEqual(TEXT("and eleven below it"), Thick.Max.Y, 231.0);

	// THE TEXT IS RECOVERABLE FROM THE TAG, which is what the drawing relies on:
	// it lays the tag out, then puts the letters back by insetting the same
	// amount. If these two ever disagreed the names would drift out of their
	// borders.
	for (ECataclysmRarity Rarity : { ECataclysmRarity::Everyday,
									 ECataclysmRarity::Masterful,
									 ECataclysmRarity::Cataclysmic })
	{
		const FBox2D Tag =
			FPickup::TagAround(Text, FPickup::NameBorderThicknessFor(Rarity));
		const double Inset = FPickup::NameBorderPaddingPx
						   + FPickup::NameBorderThicknessFor(Rarity);

		TestEqual(TEXT("insetting the tag gives the text's left edge back"),
			Tag.Min.X + Inset, Text.Min.X);
		TestEqual(TEXT("and its top edge"), Tag.Min.Y + Inset, Text.Min.Y);
	}

	// A TAG IS ALWAYS BIGGER THAN ITS TEXT, whatever the rarity. A rung that
	// shrank it would put the border through the letters.
	for (ECataclysmRarity Rarity : { ECataclysmRarity::Everyday,
									 ECataclysmRarity::Cataclysmic })
	{
		const FBox2D Tag =
			FPickup::TagAround(Text, FPickup::NameBorderThicknessFor(Rarity));
		TestTrue(TEXT("the tag contains the text"),
			Tag.Min.X < Text.Min.X && Tag.Min.Y < Text.Min.Y
			&& Tag.Max.X > Text.Max.X && Tag.Max.Y > Text.Max.Y);
	}

	// AN UNSET RECTANGLE IS LEFT ALONE. A drop that failed to project has no
	// place on screen to grow a border around.
	TestFalse(TEXT("an unset text box gives an unset tag"),
		FPickup::TagAround(
			FBox2D(),
			FPickup::NameBorderThicknessFor(ECataclysmRarity::Superb)).bIsValid);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
