// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for picking a drop up. Issue #707.
 *
 * THE FOUR JUDGEMENTS, AND WHY THEY ARE NOT IN THE HEADS-UP DISPLAY OR THE
 * CONTROLLER. Which drops are near enough to name, which name the cursor is
 * over, whether the character is near enough to take one, and what moving the
 * item actually does are all on UCataclysmDropPickup as statics. The automation
 * command runs with -nullrhi and AHUD::PostRender checks FApp::CanEverRender()
 * before calling DrawHUD, so nothing inside a draw call can be tested at all;
 * and nothing here possesses a pawn or presses a mouse button, so the input
 * handler cannot be either. Keeping the judgements out of both leaves all four
 * covered.
 *
 * THE FIRST OF THE FOUR WAS ADDED BY ISSUE #1116 and was written straight into
 * the draw call before that, which is why "a drop past ten metres draws no tag"
 * could not have been checked here.
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

	/**
	 * A pile of crafting material lying at a place. Issue #1117.
	 *
	 * A MATERIAL AND A PIECE OF GEAR ARE THE SAME ACTOR and differ only in what
	 * is filled in, which is what `ACataclysmDroppedItem::IsMaterial` reads: a
	 * material name and a quantity above zero. Both are needed, so a helper
	 * makes it hard to build half a material by accident.
	 */
	ACataclysmDroppedItem* MaterialAt(UWorld* World, const FVector& Where,
									  const TCHAR* Which, const TCHAR* Name)
	{
		ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(
			Where, FRotator::ZeroRotator);
		if (Drop)
		{
			Drop->Material = FName(Which);
			Drop->MaterialQuantity = 3;
			Drop->MaterialTier = 2;
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
// How near a drop has to be for its name to be drawn. Issue #1116.
// ---------------------------------------------------------------------------

/**
 * A name is drawn out to ten metres and no further, measured flat.
 *
 * WHAT THIS DOES NOT COVER, said plainly. It covers the rule, not the drawing:
 * that ACataclysmHUD::DrawDropNames actually asks this before naming a drop is
 * inside a draw call, and AHUD::PostRender checks FApp::CanEverRender() while
 * the automation command passes -nullrhi, so no test here can watch a frame.
 * The same gap the whole of this file has, and the reason the judgement is a
 * free function in the first place.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNameRangeTest,
	"Cataclysm.DropPickup.ANameIsDrawnOnlyWithinTenMetresMeasuredFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNameRangeTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	TestEqual(TEXT("ten metres, asked for by the project owner on 2026-08-31"),
		FPickup::NameShownRangeCm, 1000.0f);

	// THE ORDER OF THE THREE RANGES IS THE DESIGN, so it is pinned rather than
	// left to be read off three separate numbers.
	TestTrue(TEXT("a name appears further out than a click can reach, so there "
				  "is something to walk towards"),
		FPickup::NameShownRangeCm > FPickup::PickupRangeCm);
	TestTrue(TEXT("and nearer than a material's automatic sweep, which is why "
				  "a material is exempt from it altogether -- issue #1117"),
		FPickup::NameShownRangeCm < FPickup::AutomaticMaterialRangeCm);

	const FVector Standing(0.0f, 0.0f, 0.0f);

	TestTrue(TEXT("a drop underfoot is named"),
		FPickup::IsWithinNameRange(Standing, FVector(0.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("five metres away is named"),
		FPickup::IsWithinNameRange(Standing, FVector(500.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("just inside ten metres is named"),
		FPickup::IsWithinNameRange(Standing, FVector(999.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("exactly ten metres is named"),
		FPickup::IsWithinNameRange(Standing, FVector(1000.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("just outside ten metres is not"),
		FPickup::IsWithinNameRange(Standing, FVector(1001.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("thirty metres away is not"),
		FPickup::IsWithinNameRange(Standing, FVector(3000.0f, 0.0f, 0.0f)));

	// DIAGONAL, so the test cannot pass by comparing one axis. 707 on each of
	// two axes is 999.7 apart; 709 on each is 1002.6.
	TestTrue(TEXT("999.7 cm diagonally is named"),
		FPickup::IsWithinNameRange(Standing, FVector(707.0f, 707.0f, 0.0f)));
	TestFalse(TEXT("1002.6 cm diagonally is not"),
		FPickup::IsWithinNameRange(Standing, FVector(709.0f, 709.0f, 0.0f)));

	// HEIGHT IS IGNORED, for the reason the click range ignores it. Nine metres
	// away and eight metres up is 12.0 m apart in three dimensions, so a 3D test
	// would refuse it. Here that would show as a tag blinking out as the player
	// walked under a ledge the item was lying on.
	TestTrue(TEXT("a drop eight metres overhead is still named"),
		FPickup::IsWithinNameRange(Standing, FVector(900.0f, 0.0f, 800.0f)));
	TestTrue(TEXT("and one eight metres below is too"),
		FPickup::IsWithinNameRange(Standing, FVector(900.0f, 0.0f, -800.0f)));

	// AND HEIGHT DOES NOT RESCUE SOMETHING TOO FAR AWAY FLAT.
	TestFalse(TEXT("thirty metres away at the same height is still not named"),
		FPickup::IsWithinNameRange(Standing, FVector(3000.0f, 0.0f, 10.0f)));

	// IT DOES NOT MATTER WHICH WAY ROUND THE TWO ARE GIVEN.
	TestTrue(TEXT("the measure is symmetric"),
		FPickup::IsWithinNameRange(FVector(500.0f, 500.0f, 0.0f),
								   FVector(1200.0f, 500.0f, 0.0f)));

	// NOTHING REACHABLE IS EVER INVISIBLE, which is the invariant that keeps the
	// change from taking away a pick-up the player could already make. A drop
	// exactly at the edge of arm's reach still has a name, so it still has a
	// rectangle, so a click still finds it.
	const FVector AtArmsReach(FPickup::PickupRangeCm, 0.0f, 0.0f);
	TestTrue(TEXT("a drop exactly at the click range is in reach"),
		FPickup::IsWithinPickupRange(Standing, AtArmsReach));
	TestTrue(TEXT("and it is named, so it is clickable"),
		FPickup::IsWithinNameRange(Standing, AtArmsReach));

	// THE BAND BETWEEN THE TWO IS WHAT WALKING TO A DROP IS FOR. Named, so it
	// can be clicked; out of reach, so the click starts a walk rather than a
	// pick-up. ACataclysmPlayerController::Input_MoveToCursorReleased is where
	// those two answers are used together.
	const FVector Across(700.0f, 0.0f, 0.0f);
	TestFalse(TEXT("seven metres away is out of clicking reach"),
		FPickup::IsWithinPickupRange(Standing, Across));
	TestTrue(TEXT("but it is named, so it can still be walked to"),
		FPickup::IsWithinNameRange(Standing, Across));

	// AND PAST THE NAME RANGE THERE IS NOTHING TO CLICK AT ALL. This is the part
	// the project owner decided on 2026-08-31 rather than accepting silently.
	const FVector FarOff(1100.0f, 0.0f, 0.0f);
	TestFalse(TEXT("eleven metres away is not named"),
		FPickup::IsWithinNameRange(Standing, FarOff));
	TestFalse(TEXT("and it was never in clicking reach either"),
		FPickup::IsWithinPickupRange(Standing, FarOff));

	return true;
}

/**
 * Walking the level picks out the near drops and leaves the far ones behind.
 *
 * THIS IS THE ISSUE'S ACCEPTANCE, AS CLOSE AS A TEST CAN GET TO IT. The four
 * things asked for were that a drop past ten metres draws no tag, that walking
 * closer brings it back, that walking away hides it again, and that a drop
 * beyond ten metres is not clickable. The first three are checked here with real
 * drops spawned at real distances in a real world.
 *
 * THE FOURTH FOLLOWS FROM THE LIST RATHER THAN BEING CHECKED SEPARATELY.
 * ACataclysmHUD::DrawDropNames fills DropNameRects from exactly this list, and
 * ACataclysmHUD::DropUnderPoint tests a click against DropNameRects, so a drop
 * missing from here has no rectangle and no click can find it.
 *
 * WHAT IS STILL NOT COVERED, said plainly: that the heads-up display calls this
 * at all, and that a real left click reaches DropUnderPoint. Both are inside a
 * draw call or an input handler, and neither runs under -nullrhi.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropsToNameTest,
	"Cataclysm.DropPickup.OnlyTheDropsWithinTenMetresAreNamed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropsToNameTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FVector Origin(0.0f, 0.0f, 0.0f);

	ACataclysmDroppedItem* Underfoot =
		DropAt(World, Origin, TEXT("Greataxe"), TEXT("Underfoot"));
	ACataclysmDroppedItem* FiveMetres = DropAt(World,
		FVector(500.0f, 0.0f, 0.0f), TEXT("Greataxe"), TEXT("Five metres"));
	ACataclysmDroppedItem* AtTheEdge = DropAt(World,
		FVector(FPickup::NameShownRangeCm, 0.0f, 0.0f), TEXT("Greataxe"),
		TEXT("Exactly ten metres"));
	ACataclysmDroppedItem* JustPast = DropAt(World,
		FVector(FPickup::NameShownRangeCm + 1.0f, 0.0f, 0.0f), TEXT("Greataxe"),
		TEXT("One centimetre too far"));
	ACataclysmDroppedItem* FarOff = DropAt(World,
		FVector(3000.0f, 0.0f, 0.0f), TEXT("Greataxe"), TEXT("Thirty metres"));

	// UP A LEDGE, EIGHT METRES ABOVE A POINT NINE METRES AWAY. That is 12.0 m
	// apart in three dimensions and 9 m apart flat, so it is named. Measuring in
	// three dimensions would show as a tag blinking out as the player walked
	// under the ledge.
	ACataclysmDroppedItem* OnALedge = DropAt(World,
		FVector(900.0f, 0.0f, 800.0f), TEXT("Greataxe"), TEXT("On a ledge"));

	// NO NAME MEANS MALFORMED, and it is rejected wherever it lies. Underfoot,
	// so distance cannot be what rejected it.
	ACataclysmDroppedItem* Nameless =
		DropAt(World, Origin, TEXT("Greataxe"), TEXT(""));

	if (!TestNotNull(TEXT("underfoot"), Underfoot)
		|| !TestNotNull(TEXT("five metres"), FiveMetres)
		|| !TestNotNull(TEXT("at the edge"), AtTheEdge)
		|| !TestNotNull(TEXT("just past"), JustPast)
		|| !TestNotNull(TEXT("far off"), FarOff)
		|| !TestNotNull(TEXT("on a ledge"), OnALedge)
		|| !TestNotNull(TEXT("nameless"), Nameless))
	{
		return false;
	}

	// THE ARRAY ARRIVES WITH SOMETHING IN IT, so "emptied before anything is
	// added" is checked rather than assumed. A caller reusing one array frame
	// after frame is exactly what the heads-up display does.
	TArray<ACataclysmDroppedItem*> Named;
	Named.Add(FarOff);

	FPickup::DropsToName(World, Origin, Named);

	TestEqual(TEXT("four of the seven drops are named"), Named.Num(), 4);
	TestTrue(TEXT("the one underfoot is named"), Named.Contains(Underfoot));
	TestTrue(TEXT("five metres away is named"), Named.Contains(FiveMetres));
	TestTrue(TEXT("exactly ten metres away is named"),
		Named.Contains(AtTheEdge));
	TestTrue(TEXT("and the one up a ledge is, because height is ignored"),
		Named.Contains(OnALedge));

	TestFalse(TEXT("one centimetre past ten metres is not named"),
		Named.Contains(JustPast));
	TestFalse(TEXT("thirty metres away is not named, and the stale entry the "
				   "array arrived with is gone"), Named.Contains(FarOff));
	TestFalse(TEXT("and a drop with no name is not named, underfoot or not"),
		Named.Contains(Nameless));

	// WALKING OVER TO THE FAR ONE BRINGS IT BACK. Standing beside the thirty
	// metre drop, it is named and the ones near the origin are not. This is the
	// "comes back when they are within ten metres again" half of the request,
	// and it is the same call with a different position rather than any state
	// that has to be reset.
	FPickup::DropsToName(World, FVector(3000.0f, 0.0f, 0.0f), Named);

	TestTrue(TEXT("standing beside it, the far drop is named"),
		Named.Contains(FarOff));
	TestFalse(TEXT("and the one underfoot back at the origin is not"),
		Named.Contains(Underfoot));
	TestFalse(TEXT("nor the one five metres from the origin"),
		Named.Contains(FiveMetres));

	// AND WALKING BACK HIDES IT AGAIN, which is the other half and is not the
	// same statement: a rule that only ever added drops would pass everything
	// above.
	FPickup::DropsToName(World, Origin, Named);
	TestFalse(TEXT("back at the origin the far drop is hidden again"),
		Named.Contains(FarOff));
	TestTrue(TEXT("and the one underfoot is named again"),
		Named.Contains(Underfoot));

	// A DESTROYED DROP LEAVES THE LIST. A drop just picked up must not be named
	// for another frame, or a click at its old position would find it.
	Underfoot->Destroy();
	FPickup::DropsToName(World, Origin, Named);
	TestEqual(TEXT("three are named once one has been taken"), Named.Num(), 3);
	TestFalse(TEXT("and the destroyed one is not among them"),
		Named.Contains(Underfoot));

	// NO WORLD NAMES NOTHING, rather than reading through a null pointer.
	FPickup::DropsToName(nullptr, Origin, Named);
	TestEqual(TEXT("no world names nothing"), Named.Num(), 0);

	return true;
}

/**
 * A crafting material is named however far away it is, and gear is not.
 *
 * WHY MATERIALS ARE EXEMPT AT ALL. Issue #1117. The ten metre name range is
 * shorter than the fifteen metre range at which a material is collected without
 * being clicked, and that collection runs every frame, so a material was only
 * ever still lying on the floor when it was already too far away to be named.
 * The two windows did not overlap and a material's name was therefore never
 * drawn in play at all. The project owner chose on 2026-09-01 to exempt
 * materials rather than accept that.
 *
 * WHAT A PLAYER SEES because of it: a material named across the room, which
 * winks out as they walk into the fifteen metres and it comes to them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialsAreNamedAtAnyDistanceTest,
	"Cataclysm.DropPickup.ACraftingMaterialIsNamedAtAnyDistanceAndGearIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialsAreNamedAtAnyDistanceTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FVector Origin(0.0f, 0.0f, 0.0f);

	// THE SAME TWO DISTANCES FOR EACH KIND, so the only thing that differs
	// between the pairs is whether the drop is a material.
	const FVector Near(500.0f, 0.0f, 0.0f);
	const FVector FarOff(5000.0f, 0.0f, 0.0f);

	ACataclysmDroppedItem* NearGear =
		DropAt(World, Near, TEXT("Greataxe"), TEXT("Gear five metres"));
	ACataclysmDroppedItem* FarGear =
		DropAt(World, FarOff, TEXT("Greataxe"), TEXT("Gear fifty metres"));
	ACataclysmDroppedItem* NearMaterial =
		MaterialAt(World, Near, TEXT("IronOre"), TEXT("Iron Ore five metres"));
	ACataclysmDroppedItem* FarMaterial =
		MaterialAt(World, FarOff, TEXT("IronOre"), TEXT("Iron Ore fifty metres"));

	if (!TestNotNull(TEXT("near gear"), NearGear)
		|| !TestNotNull(TEXT("far gear"), FarGear)
		|| !TestNotNull(TEXT("near material"), NearMaterial)
		|| !TestNotNull(TEXT("far material"), FarMaterial))
	{
		return false;
	}

	// THE HELPER REALLY BUILT A MATERIAL AND REALLY DID NOT for the gear. Both
	// are asserted, because every claim below rests on this one test and a
	// helper that quietly built two pieces of gear would make the whole thing
	// pass for the wrong reason.
	TestTrue(TEXT("the material drops are materials"),
			 NearMaterial->IsMaterial() && FarMaterial->IsMaterial());
	TestFalse(TEXT("and the gear drops are not"),
			  NearGear->IsMaterial() || FarGear->IsMaterial());

	TArray<ACataclysmDroppedItem*> Named;
	FPickup::DropsToName(World, Origin, Named);

	// FIFTY METRES IS FIVE TIMES THE NAME RANGE AND MORE THAN THREE TIMES THE
	// SWEEP, so nothing about either distance is borderline.
	TestTrue(TEXT("a material five metres away is named"),
			 Named.Contains(NearMaterial));
	TestTrue(TEXT("and one fifty metres away is named too"),
			 Named.Contains(FarMaterial));

	TestTrue(TEXT("gear five metres away is named"), Named.Contains(NearGear));
	TestFalse(TEXT("and gear fifty metres away is not, which is the rule the "
				   "material is exempt from"),
			  Named.Contains(FarGear));

	TestEqual(TEXT("three of the four drops are named"), Named.Num(), 3);

	// AND MOVING DOES NOT CHANGE THE MATERIAL'S ANSWER, which is what "at any
	// distance" means and what a single position could not show. Standing on
	// top of the far pair, the near gear is now the one out of range and both
	// materials are still named.
	FPickup::DropsToName(World, FarOff, Named);
	TestTrue(TEXT("standing beside the far material, it is still named"),
			 Named.Contains(FarMaterial));
	TestTrue(TEXT("and the material now fifty metres away is still named"),
			 Named.Contains(NearMaterial));
	TestTrue(TEXT("the gear underfoot is named"), Named.Contains(FarGear));
	TestFalse(TEXT("and the gear now fifty metres away is not"),
			  Named.Contains(NearGear));

	// A MATERIAL WITH NO NAME IS STILL REJECTED. The exemption is from the
	// distance rule and from nothing else, so a malformed drop is not drawn
	// just because it is a material.
	ACataclysmDroppedItem* Nameless =
		MaterialAt(World, FarOff, TEXT("IronOre"), TEXT(""));
	if (!TestNotNull(TEXT("nameless material"), Nameless))
	{
		return false;
	}
	FPickup::DropsToName(World, Origin, Named);
	TestFalse(TEXT("a material with no name is not named"),
			  Named.Contains(Nameless));
	TestEqual(TEXT("so the count is unchanged"), Named.Num(), 3);

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


// ---------------------------------------------------------------------------
// A crafting material coming to the character on its own. Issue #851.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialsComeToTheCharacter,
	"Cataclysm.Drop.ACraftingMaterialComesToTheCharacterAndGearDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialsComeToTheCharacter::RunTest(const FString& Parameters)
{
	using FPickup = UCataclysmDropPickup;

	const FVector Standing(0.0f, 0.0f, 0.0f);
	constexpr bool Material = true;
	constexpr bool Gear = false;

	// FIVE TIMES THE CLICK RANGE, decided by the project owner on 2026-08-23.
	// Compared against the click range rather than written out as 1500, so
	// tuning one and forgetting the other fails here.
	TestEqual(TEXT("the automatic range is five times the click range"),
		FPickup::AutomaticMaterialRangeCm, FPickup::PickupRangeCm * 5.0f);

	// GEAR NEVER COMES, AT ANY DISTANCE, INCLUDING NONE. This is the rule, not a
	// consequence of a radius: a piece of gear is a decision and has to be walked
	// to and looked at.
	TestFalse(TEXT("gear underfoot does not come"),
		FPickup::ComesAutomatically(Gear, Standing, Standing));
	TestFalse(TEXT("gear one metre away does not come"),
		FPickup::ComesAutomatically(Gear, Standing, FVector(100.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("gear well inside the automatic range does not come"),
		FPickup::ComesAutomatically(Gear, Standing, FVector(500.0f, 0.0f, 0.0f)));

	// A MATERIAL DOES, OUT TO THE STATED RANGE.
	TestTrue(TEXT("a material underfoot comes"),
		FPickup::ComesAutomatically(Material, Standing, Standing));
	TestTrue(TEXT("a material ten metres away comes"),
		FPickup::ComesAutomatically(Material, Standing,
									FVector(1000.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("a material exactly at the range comes"),
		FPickup::ComesAutomatically(Material, Standing,
			FVector(FPickup::AutomaticMaterialRangeCm, 0.0f, 0.0f)));
	TestFalse(TEXT("a material one centimetre past the range does not"),
		FPickup::ComesAutomatically(Material, Standing,
			FVector(FPickup::AutomaticMaterialRangeCm + 1.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("a material thirty metres away does not"),
		FPickup::ComesAutomatically(Material, Standing,
									FVector(3000.0f, 0.0f, 0.0f)));

	// DIAGONAL, so the check cannot pass by comparing one axis. 1060 on each of
	// two axes is 1499 apart; 1062 on each is 1502.
	TestTrue(TEXT("1499 cm diagonally comes"),
		FPickup::ComesAutomatically(Material, Standing,
									FVector(1060.0f, 1060.0f, 0.0f)));
	TestFalse(TEXT("1502 cm diagonally does not"),
		FPickup::ComesAutomatically(Material, Standing,
									FVector(1062.0f, 1062.0f, 0.0f)));

	// HEIGHT IS IGNORED, for the same reason the click range ignores it: a drop
	// from a tall creature, or one that died on a step, must not be quietly
	// harder to collect than the same drop on flat ground. Ten metres away and
	// twenty metres up is 22.4 m apart in three dimensions, so a 3D test would
	// refuse it.
	TestTrue(TEXT("a material twenty metres overhead still comes"),
		FPickup::ComesAutomatically(Material, Standing,
									FVector(1000.0f, 0.0f, 2000.0f)));
	TestTrue(TEXT("and one twenty metres below does too"),
		FPickup::ComesAutomatically(Material, Standing,
									FVector(1000.0f, 0.0f, -2000.0f)));

	// IT REACHES FURTHER THAN A CLICK AND THAT IS THE POINT. A material a player
	// would have had to click for is now collected by walking near it.
	const FVector Beyond(FPickup::PickupRangeCm + 100.0f, 0.0f, 0.0f);
	TestFalse(TEXT("that spot is out of clicking reach"),
		FPickup::IsWithinPickupRange(Standing, Beyond));
	TestTrue(TEXT("and a material there still comes on its own"),
		FPickup::ComesAutomatically(Material, Standing, Beyond));

	return true;
}

// --------------------------------------------------------------------------
// Putting one on the floor, which is the other direction. Issue #1190.
// --------------------------------------------------------------------------

/**
 * Tests for taking an item out of the bag and leaving it on the floor.
 *
 * WHY THIS MATTERS MORE THAN CONVENIENCE. `docs/Cataclysm_GDD_v2.md` gives this
 * game no town portal, so a player who fills the bag part way down a dungeon
 * cannot leave, cannot sell and cannot destroy anything. Dropping is the only
 * release valve there is, and until issue #1190 it did not exist at all -- not
 * by click, not by key, and not by console.
 *
 * THE ONE THING WORTH GUARDING IS THE SAME ONE `CataclysmWearingTests.cpp`
 * guards: **nothing is ever destroyed**. Dropping is the first operation in the
 * project that takes an item out of both the bag and the body, so the count
 * that file checks -- worn plus carried -- deliberately falls here. What must
 * hold instead is that the item is on the floor when it leaves the bag and in
 * the bag when it is not, and never in neither. The refusal test below is the
 * one that checks the "neither" case cannot happen.
 */
namespace CataclysmDropFromBagTest
{
	/** How many drops are lying in this world. */
	int32 OnTheFloor(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}

	/** The one drop lying in this world, or null when there is not exactly one. */
	ACataclysmDroppedItem* TheOnlyDrop(UWorld* World)
	{
		ACataclysmDroppedItem* Found = nullptr;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (!IsValid(*It))
			{
				continue;
			}
			if (Found)
			{
				return nullptr;
			}
			Found = *It;
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropLeavesTheBagTest,
	"Cataclysm.DropPickup.DroppingTakesTheItemOutOfTheBagAndPutsItOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropLeavesTheBagTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;
	using namespace CataclysmDropFromBagTest;

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

	const int32 Slot = Inventory->AddItem(SomeItem(TEXT("Greataxe")));
	Inventory->AddItem(SomeItem(TEXT("Head_Helm")));
	TestEqual(TEXT("two items are carried to begin with"),
		Inventory->NumItems(), 2);
	TestEqual(TEXT("and the floor is empty"), OnTheFloor(World), 0);

	const ECataclysmWearResult Result = UCataclysmWearing::DropCarried(
		Inventory, Slot, World, FVector(250.0f, 0.0f, 0.0f));

	TestEqual(TEXT("it was dropped"), Result, ECataclysmWearResult::Dropped);
	TestEqual(TEXT("one item is left in the bag"), Inventory->NumItems(), 1);
	TestEqual(TEXT("and exactly one is on the floor"), OnTheFloor(World), 1);

	// THE SLOT IT CAME OUT OF IS EMPTY AND THE OTHER IS UNTOUCHED. Removing the
	// wrong slot would keep both counts right and lose the wrong item.
	TestNull(TEXT("the slot it came out of is empty"), Inventory->ItemAt(Slot));

	ACataclysmDroppedItem* Drop = TheOnlyDrop(World);
	if (!TestNotNull(TEXT("the drop"), Drop))
	{
		return false;
	}

	// THE WHOLE ITEM, not just its base. A drop that kept only the base would
	// silently strip every roll off anything the player put down.
	TestEqual(TEXT("the base went with it"), Drop->Item.Base,
		FName(TEXT("Greataxe")));
	TestEqual(TEXT("the upgrade level went with it"), Drop->Item.GearLevel, 3);
	TestEqual(TEXT("the sockets went with it"), Drop->Item.Sockets, 1);
	TestEqual(TEXT("the residue went with it"), Drop->Item.Residue, 42.0f);

	// AND IT LANDED WHERE IT WAS ASKED TO. A drop that ignored the location
	// would sit at the world origin, which is issue #723 all over again.
	TestEqual(TEXT("it is lying where it was put"), Drop->GetActorLocation(),
		FVector(250.0f, 0.0f, 0.0f));

	return true;
}

/**
 * The round trip, which is what makes dropping without a confirmation safe.
 *
 * The project owner chose on 2026-09-02 that a drop asks nothing at any rarity.
 * That is only reasonable if the item can be picked straight back up, so this
 * checks the two directions compose and the item survives both.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropThenPickUpTest,
	"Cataclysm.DropPickup.ADroppedItemCanBePickedStraightBackUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropThenPickUpTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;
	using namespace CataclysmDropFromBagTest;

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

	const int32 Slot = Inventory->AddItem(SomeItem(TEXT("Greataxe")));
	TestEqual(TEXT("it was dropped"),
		UCataclysmWearing::DropCarried(Inventory, Slot, World,
									   FVector(150.0f, 0.0f, 0.0f)),
		ECataclysmWearResult::Dropped);
	TestEqual(TEXT("the bag is empty"), Inventory->NumItems(), 0);

	ACataclysmDroppedItem* Drop = TheOnlyDrop(World);
	if (!TestNotNull(TEXT("the drop"), Drop))
	{
		return false;
	}

	TestTrue(TEXT("it was taken back"), FPickup::TakeInto(Inventory, Drop));
	TestEqual(TEXT("it is carried again"), Inventory->NumItems(), 1);
	TestEqual(TEXT("and nothing is left lying there"), OnTheFloor(World), 0);

	const FCataclysmItem* Back = Inventory->ItemAt(0);
	if (!TestNotNull(TEXT("back in the bag"), Back))
	{
		return false;
	}

	// THE SAME ITEM CAME BACK. A round trip that reset the rolls would be a way
	// to lose everything about an item by putting it down and picking it up.
	TestEqual(TEXT("the base survived the round trip"), Back->Base,
		FName(TEXT("Greataxe")));
	TestEqual(TEXT("the upgrade level survived"), Back->GearLevel, 3);
	TestEqual(TEXT("the sockets survived"), Back->Sockets, 1);
	TestEqual(TEXT("the residue survived"), Back->Residue, 42.0f);

	return true;
}

/**
 * The drop lands inside pickup range, which is what the round trip above relies
 * on being true in the running game rather than at a location a test chose.
 *
 * NOT A RESTATEMENT OF THE CONSTANT. It spawns a real actor, faces it, asks
 * UCataclysmDropSpawner::DropSpotInFrontOf where a drop would go, and measures
 * that against UCataclysmDropPickup::IsWithinPickupRange -- the same function
 * the game uses to decide whether a click can reach a drop. Setting
 * DropInFrontCm above the pickup range would fail this.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropLandsWithinReachTest,
	"Cataclysm.DropPickup.ADroppedItemLandsWithinReachOfTheCharacterWhoDroppedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropLandsWithinReachTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Character = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("a character to drop from"), Character))
	{
		return false;
	}

	// FOUR DIRECTIONS, because a spot worked out with the forward vector ignored
	// would be within range facing one way and wrong facing another.
	const float Yaws[] = { 0.0f, 90.0f, 180.0f, -45.0f };
	for (const float Yaw : Yaws)
	{
		Character->SetActorLocation(FVector(1000.0f, -400.0f, 90.0f));
		Character->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));

		const FVector Spot =
			UCataclysmDropSpawner::DropSpotInFrontOf(*Character);

		TestTrue(*FString::Printf(
			TEXT("facing %.0f degrees, the drop is close enough to pick back up"),
			Yaw),
			FPickup::IsWithinPickupRange(Character->GetActorLocation(), Spot));

		// AND NOT UNDER FOOT. The heads-up display draws the drop's name at its
		// position, so a drop at the character's own feet is a name behind the
		// character.
		TestTrue(*FString::Printf(
			TEXT("facing %.0f degrees, it is not on top of the character"), Yaw),
			FVector::Dist2D(Character->GetActorLocation(), Spot) > 50.0f);
	}

	return true;
}

/**
 * Dropping an empty slot, or one outside the grid, does nothing.
 *
 * THE CONSOLE COMMAND TAKES A TYPED NUMBER, so a slot outside 0 to 47 is a
 * thing a person will actually pass, and it must not be treated as slot 0.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropNothingTest,
	"Cataclysm.DropPickup.DroppingAnEmptyOrImpossibleSlotDoesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropNothingTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;
	using namespace CataclysmDropFromBagTest;

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

	Inventory->AddItem(SomeItem(TEXT("Greataxe")));

	// SLOT 0 IS TAKEN, SO SLOT 1 IS THE EMPTY ONE. Asking for an occupied slot
	// here would prove nothing.
	const int32 Cases[] = { 1, 47, -1, 48, 5000 };
	for (const int32 Slot : Cases)
	{
		TestEqual(*FString::Printf(TEXT("slot %d has nothing to drop"), Slot),
			UCataclysmWearing::DropCarried(Inventory, Slot, World,
										   FVector::ZeroVector),
			ECataclysmWearResult::NothingToDrop);
	}

	TestEqual(TEXT("the carried item is untouched"), Inventory->NumItems(), 1);
	TestEqual(TEXT("and nothing was put on the floor"), OnTheFloor(World), 0);

	return true;
}

/**
 * A crafting material drops as its whole stack, with the count intact.
 *
 * A SLOT HOLDS ONE STACK AND THIS EMPTIES ONE SLOT. Dropping part of a stack
 * would need a quantity to be chosen and there is no screen for choosing one.
 * The quantity is the part that would be easy to lose: ACataclysmDroppedItem
 * carries the material name and the count in two separate fields, and a drop
 * that set only the name would put a stack of zero on the floor.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropMaterialStackTest,
	"Cataclysm.DropPickup.DroppingACraftingMaterialDropsTheWholeStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropMaterialStackTest::RunTest(const FString&)
{
	using namespace CataclysmDropFromBagTest;

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

	const int32 Slot =
		Inventory->AddMaterial(FName(TEXT("Material_Corrupted_Mote")), 7);
	// TestTrue RATHER THAN TestNotEqual, because INDEX_NONE is an untyped -1 and
	// the overload set cannot pick between its int32 and int64 forms.
	if (!TestTrue(TEXT("the materials went in"), Slot != INDEX_NONE))
	{
		return false;
	}

	TestEqual(TEXT("they were dropped"),
		UCataclysmWearing::DropCarried(Inventory, Slot, World,
									   FVector(150.0f, 0.0f, 0.0f)),
		ECataclysmWearResult::Dropped);
	TestEqual(TEXT("the bag is empty"), Inventory->NumItems(), 0);

	ACataclysmDroppedItem* Drop = TheOnlyDrop(World);
	if (!TestNotNull(TEXT("the drop"), Drop))
	{
		return false;
	}

	TestTrue(TEXT("what is lying there is a material"), Drop->IsMaterial());
	TestEqual(TEXT("it is the material that was carried"), Drop->Material,
		FName(TEXT("Material_Corrupted_Mote")));
	TestEqual(TEXT("and all seven went, not one"), Drop->MaterialQuantity, 7);

	return true;
}

/**
 * The cursor is released when the slot dropped was the one being held, and only
 * then.
 *
 * A SCREEN LEFT HOLDING A SLOT WHOSE ITEM IS ON THE FLOOR would draw an item on
 * the cursor that is no longer in the bag, and the next click would put down
 * something that does not exist. The second half matters because
 * `Cataclysm.Drop` can be typed while a screen is open holding a different
 * slot, and that must not silently put the other item down.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropReleasesTheCursorTest,
	"Cataclysm.DropPickup.DroppingTheHeldSlotReleasesTheCursorAndAnotherSlotDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropReleasesTheCursorTest::RunTest(const FString&)
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

	const int32 Held = Inventory->AddItem(SomeItem(TEXT("Greataxe")));
	const int32 Other = Inventory->AddItem(SomeItem(TEXT("Head_Helm")));

	TestEqual(TEXT("it was picked up onto the cursor"),
		UCataclysmWearing::PickUpCarried(Inventory, Held),
		ECataclysmWearResult::PickedUp);
	TestTrue(TEXT("something is held"), Inventory->IsHolding());

	// A DIFFERENT SLOT FIRST. The cursor must survive this.
	TestEqual(TEXT("the other slot was dropped"),
		UCataclysmWearing::DropCarried(Inventory, Other, World,
									   FVector(150.0f, 0.0f, 0.0f)),
		ECataclysmWearResult::Dropped);
	TestTrue(TEXT("the cursor still holds what it held"),
		Inventory->IsHolding());
	TestEqual(TEXT("and it still points at the same slot"),
		Inventory->HeldSlot(), Held);

	// NOW THE HELD ONE.
	TestEqual(TEXT("the held slot was dropped"),
		UCataclysmWearing::DropCarried(Inventory, Held, World,
									   FVector(150.0f, 0.0f, 0.0f)),
		ECataclysmWearResult::Dropped);
	TestFalse(TEXT("the cursor is empty now"), Inventory->IsHolding());
	TestEqual(TEXT("and points at nothing"), Inventory->HeldSlot(), INDEX_NONE);

	return true;
}

/**
 * When the floor refuses the item, the item is still in the bag.
 *
 * THIS IS THE ONE THAT PROVES NOTHING IS DESTROYED. Every other test here
 * checks a path that worked. The order inside DropCarried -- spawn first, empty
 * the slot second -- exists only for this case, and reversing it would pass
 * every other test in this file while quietly deleting a player's item whenever
 * a spawn failed.
 *
 * A NULL WORLD IS HOW THE FAILURE IS REACHED. A drop is asked for with
 * AlwaysSpawn, so a world that exists will not refuse one; the real cases are a
 * world that is missing or shutting down.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropRefusedKeepsTheItemTest,
	"Cataclysm.DropPickup.AnItemTheFloorRefusesIsStillInTheBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropRefusedKeepsTheItemTest::RunTest(const FString&)
{
	using namespace CataclysmDropPickupTest;

	UCataclysmInventoryComponent* Inventory =
		NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	const int32 Slot = Inventory->AddItem(SomeItem(TEXT("Greataxe")));

	TestEqual(TEXT("the floor refused it"),
		UCataclysmWearing::DropCarried(Inventory, Slot, /*World=*/nullptr,
									   FVector::ZeroVector),
		ECataclysmWearResult::TheFloorRefusedIt);

	TestEqual(TEXT("it is still carried"), Inventory->NumItems(), 1);

	const FCataclysmItem* Kept = Inventory->ItemAt(Slot);
	if (!TestNotNull(TEXT("still in the slot it was in"), Kept))
	{
		return false;
	}
	TestEqual(TEXT("and it is the same item"), Kept->Base,
		FName(TEXT("Greataxe")));
	TestEqual(TEXT("with its rolls intact"), Kept->Residue, 42.0f);

	// AND WITHOUT AN INVENTORY THERE IS NOTHING TO WORK WITH, which is a
	// different answer from "the floor refused it" on purpose: one means the
	// item is safe and the other means there was no item.
	TestEqual(TEXT("no inventory is a different answer"),
		UCataclysmWearing::DropCarried(nullptr, 0, nullptr, FVector::ZeroVector),
		ECataclysmWearResult::NothingToWorkWith);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
