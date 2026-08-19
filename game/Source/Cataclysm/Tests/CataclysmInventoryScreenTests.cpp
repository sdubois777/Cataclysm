// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"

/**
 * Tests for the carried inventory screen. Issue #731.
 *
 * WHAT IS COVERED AND WHAT CANNOT BE. Every judgement the screen makes is a
 * static function on UCataclysmInventoryScreen and every one of them is checked
 * here: how big a cell is for a viewport, where each of the 48 sits, whether a
 * point is on the panel, what a slot's label says, how it is broken into lines,
 * what colour and how thick its frame is, and what the header line reads.
 *
 * WHAT IS THEREFORE NOT COVERED, said plainly: that ACataclysmHUD::DrawInventory
 * calls any of it, that the panel is legible, that pressing I reaches
 * ToggleInventory, and that a click on a cell really stops being a move order.
 * AHUD::PostRender checks FApp::CanEverRender() before calling DrawHUD and the
 * automation command in tools/unreal_build.py passes -nullrhi, so DrawHUD does
 * not run here at all, and nothing in this file possesses a pawn or presses a
 * key. Those four were checked by playing. It is the same wall
 * CataclysmDropPickupTests.cpp records for the drop names.
 */
namespace CataclysmInventoryScreenTest
{
	using FScreen = UCataclysmInventoryScreen;

	/** A slot holding a piece of gear of a given rarity. */
	FCataclysmCarriedSlot GearSlot(const TCHAR* Base, int32 Affixes,
								   int32 Enchantments = 0)
	{
		FCataclysmCarriedSlot Slot;
		Slot.Item.Base = FName(Base);
		Slot.Item.Affixes.SetNum(Affixes);
		Slot.Item.EnchantmentCount = Enchantments;
		return Slot;
	}

	/** A slot holding a stack of one crafting material. */
	FCataclysmCarriedSlot MaterialSlot(const TCHAR* Material, int32 Quantity)
	{
		FCataclysmCarriedSlot Slot;
		Slot.Material = FName(Material);
		Slot.Quantity = Quantity;
		return Slot;
	}

	/** How many affixes a rarity is made of, so a test can build one. */
	int32 AffixesFor(ECataclysmRarity Rarity)
	{
		return UCataclysmItemValues::AffixSlotsFor(Rarity);
	}

	int32 EnchantmentsFor(ECataclysmRarity Rarity)
	{
		return UCataclysmItemValues::EnchantmentsFor(Rarity);
	}

	/** A slot holding gear that really is this rarity. */
	FCataclysmCarriedSlot SlotOfRarity(const TCHAR* Base,
									   ECataclysmRarity Rarity)
	{
		return GearSlot(Base, AffixesFor(Rarity), EnchantmentsFor(Rarity));
	}
}

// ---------------------------------------------------------------------------
// The grid fits on the screen
// ---------------------------------------------------------------------------

/**
 * The whole panel is inside the viewport, at every shape of viewport.
 *
 * THIS IS THE FAILURE THE CELL SIZE EXISTS TO PREVENT. Twelve cells side by side
 * is a wide thing, so a fixed cell size that looked right on a wide monitor puts
 * the last column past the right edge on a narrower one, where it cannot be
 * clicked or read and nothing says why.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryPanelFitsTest,
	"Cataclysm.InventoryScreen.ThePanelFitsInsideEveryViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryPanelFitsTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	struct FViewport
	{
		float Width;
		float Height;
	};

	// Sixteen by nine, sixteen by ten, four by three, a tall window, an
	// ultra-wide monitor, and a viewport small enough to be silly.
	const FViewport Viewports[] = {
		{ 3840.0f, 2160.0f }, { 2560.0f, 1440.0f }, { 1920.0f, 1080.0f },
		{ 1600.0f, 1000.0f }, { 1280.0f,  720.0f }, { 1024.0f,  768.0f },
		{  800.0f,  600.0f }, {  720.0f, 1280.0f }, { 3440.0f, 1440.0f },
		{  400.0f,  300.0f },
	};

	for (const FViewport& Viewport : Viewports)
	{
		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"),
											  Viewport.Width, Viewport.Height);

		const float CellPx = FScreen::CellSizeFor(Viewport.Width,
												  Viewport.Height);
		TestTrue(*(Where + TEXT(": a cell has a positive size")),
			CellPx > 0.0f);
		TestTrue(*(Where + TEXT(": and is no bigger than the ceiling")),
			CellPx <= FScreen::MaxCellPx);

		const FBox2D Panel = FScreen::PanelRectFor(Viewport.Width,
												   Viewport.Height);
		TestTrue(*(Where + TEXT(": the panel's left edge is on screen")),
			Panel.Min.X >= 0.0);
		TestTrue(*(Where + TEXT(": its top edge is on screen")),
			Panel.Min.Y >= 0.0);
		TestTrue(*(Where + TEXT(": its right edge is on screen")),
			Panel.Max.X <= Viewport.Width);
		TestTrue(*(Where + TEXT(": its bottom edge is on screen")),
			Panel.Max.Y <= Viewport.Height);

		// AND EVERY CELL IS INSIDE THE PANEL, which is the part that matters:
		// a panel that fits with a column hanging out of it would pass the four
		// checks above.
		for (int32 Slot = 0; Slot < UCataclysmInventoryComponent::SlotCount;
			 ++Slot)
		{
			const FBox2D Cell = FScreen::CellRectFor(Panel, CellPx, Slot);
			if (!Cell.bIsValid)
			{
				AddError(FString::Printf(TEXT("%s: slot %d has no cell"),
										 *Where, Slot));
				continue;
			}

			if (Cell.Min.X < Panel.Min.X || Cell.Min.Y < Panel.Min.Y
				|| Cell.Max.X > Panel.Max.X || Cell.Max.Y > Panel.Max.Y)
			{
				AddError(FString::Printf(
					TEXT("%s: slot %d sits outside the panel"), *Where, Slot));
			}
		}
	}

	// THE CEILING REALLY BINDS somewhere, or it is not a ceiling. A very wide
	// viewport has room for far more than MaxCellPx and must not use it.
	TestEqual(TEXT("a huge viewport draws cells at the ceiling size"),
		FScreen::CellSizeFor(7680.0f, 4320.0f), FScreen::MaxCellPx);

	// AND THE VIEWPORT REALLY BINDS somewhere too, or the ceiling is the only
	// rule and the fitting is untested.
	TestTrue(TEXT("a narrow viewport draws them smaller than the ceiling"),
		FScreen::CellSizeFor(800.0f, 600.0f) < FScreen::MaxCellPx);

	return true;
}

// ---------------------------------------------------------------------------
// Where each cell sits
// ---------------------------------------------------------------------------

/**
 * The 48 cells are four rows of twelve, in reading order, and none overlaps.
 *
 * READING ORDER IS THE PART A PLAYER SEES. UCataclysmInventoryComponent::AddItem
 * fills the lowest free slot, so an item picked up has to appear at the first
 * gap counting left to right and then down. A grid numbered down its columns
 * would put it somewhere the player has no reason to look.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryCellLayoutTest,
	"Cataclysm.InventoryScreen.TheCellsAreFourRowsOfTwelveInReadingOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryCellLayoutTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	const float Width = 1920.0f;
	const float Height = 1080.0f;
	const FBox2D Panel = FScreen::PanelRectFor(Width, Height);
	const float CellPx = FScreen::CellSizeFor(Width, Height);

	const int32 Columns = UCataclysmInventoryComponent::Columns;

	// SLOT 0 IS THE TOP LEFT, under the header rather than over it.
	const FBox2D First = FScreen::CellRectFor(Panel, CellPx, 0);
	TestEqual(TEXT("slot 0 starts one padding in from the left"),
		First.Min.X, Panel.Min.X + FScreen::PanelPaddingPx);
	TestEqual(TEXT("and below the header band"),
		First.Min.Y,
		Panel.Min.Y + FScreen::PanelPaddingPx + FScreen::HeaderHeightPx);
	TestEqual(TEXT("a cell is square"),
		First.Max.X - First.Min.X, First.Max.Y - First.Min.Y);

	// SLOT 11 IS THE END OF THE FIRST ROW, not the start of the second.
	const FBox2D Eleventh = FScreen::CellRectFor(Panel, CellPx, Columns - 1);
	TestEqual(TEXT("slot 11 is on the first row"), Eleventh.Min.Y, First.Min.Y);
	TestTrue(TEXT("and to the right of slot 0"),
		Eleventh.Min.X > First.Min.X);
	TestEqual(TEXT("its right edge is one padding in from the panel's"),
		Eleventh.Max.X, Panel.Max.X - FScreen::PanelPaddingPx);

	// SLOT 12 STARTS THE SECOND ROW, back at the left.
	const FBox2D Twelfth = FScreen::CellRectFor(Panel, CellPx, Columns);
	TestEqual(TEXT("slot 12 is back at the left edge"),
		Twelfth.Min.X, First.Min.X);
	TestEqual(TEXT("and one row down"),
		Twelfth.Min.Y, First.Min.Y + CellPx + FScreen::CellGapPx);

	// SLOT 47 IS THE BOTTOM RIGHT.
	const FBox2D Last = FScreen::CellRectFor(
		Panel, CellPx, UCataclysmInventoryComponent::SlotCount - 1);
	TestEqual(TEXT("slot 47's right edge matches slot 11's"),
		Last.Max.X, Eleventh.Max.X);
	TestEqual(TEXT("and its bottom edge is one padding up from the panel's"),
		Last.Max.Y, Panel.Max.Y - FScreen::PanelPaddingPx);

	// NO TWO CELLS OVERLAP. Checked in full rather than by sampling: 48 by 48 is
	// 1,128 comparisons and a layout is exactly the kind of thing that is right
	// everywhere except one corner.
	TArray<FBox2D> Cells;
	for (int32 Slot = 0; Slot < UCataclysmInventoryComponent::SlotCount; ++Slot)
	{
		Cells.Add(FScreen::CellRectFor(Panel, CellPx, Slot));
	}

	for (int32 A = 0; A < Cells.Num(); ++A)
	{
		for (int32 B = A + 1; B < Cells.Num(); ++B)
		{
			if (Cells[A].Intersect(Cells[B]))
			{
				AddError(FString::Printf(
					TEXT("slots %d and %d overlap"), A, B));
			}
		}
	}

	// A SLOT NUMBER OUTSIDE THE GRID HAS NO CELL, rather than one off the end.
	TestFalse(TEXT("slot -1 has no cell"),
		FScreen::CellRectFor(Panel, CellPx, -1).bIsValid);
	TestFalse(TEXT("slot 48 has no cell"),
		FScreen::CellRectFor(Panel, CellPx,
							 UCataclysmInventoryComponent::SlotCount).bIsValid);
	TestFalse(TEXT("and neither does one with no panel"),
		FScreen::CellRectFor(FBox2D(ForceInit), CellPx, 0).bIsValid);

	return true;
}

// ---------------------------------------------------------------------------
// A click on the panel is not a click on the world
// ---------------------------------------------------------------------------

/**
 * The panel covers the points it was drawn over, edges included.
 *
 * WHAT GOES WRONG WITHOUT IT. The left mouse button orders a move and the cursor
 * ray passes through anything drawn on the canvas, so a click on a grid cell
 * would send the character walking to whatever floor lies behind the panel.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryPanelCoversPointTest,
	"Cataclysm.InventoryScreen.ThePanelSwallowsAClickOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryPanelCoversPointTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	const FBox2D Panel = FScreen::PanelRectFor(1920.0f, 1080.0f);

	TestTrue(TEXT("the middle of the panel is covered"),
		FScreen::PanelCoversPoint(Panel, (Panel.Min + Panel.Max) * 0.5));

	// THE EDGE IS COVERED, WHICH THE STRICT TEST WOULD MISS. The alternative to
	// a click on the boundary being caught is not a click that misses; it is a
	// move order the player did not give.
	TestTrue(TEXT("the top left corner is covered"),
		FScreen::PanelCoversPoint(Panel, Panel.Min));
	TestTrue(TEXT("the bottom right corner is covered"),
		FScreen::PanelCoversPoint(Panel, Panel.Max));

	// EVERY CELL IS COVERED, so no gap between cells is a hole through it.
	const float CellPx = FScreen::CellSizeFor(1920.0f, 1080.0f);
	for (int32 Slot = 0; Slot < UCataclysmInventoryComponent::SlotCount; ++Slot)
	{
		const FBox2D Cell = FScreen::CellRectFor(Panel, CellPx, Slot);
		if (!FScreen::PanelCoversPoint(Panel, Cell.GetCenter()))
		{
			AddError(FString::Printf(
				TEXT("slot %d's centre is not on the panel"), Slot));
		}
		if (!FScreen::PanelCoversPoint(Panel, Cell.Min)
			|| !FScreen::PanelCoversPoint(Panel, Cell.Max))
		{
			AddError(FString::Printf(
				TEXT("slot %d has a corner off the panel"), Slot));
		}
	}

	// AND NOTHING OUTSIDE IT IS, or the whole screen would stop moving the
	// character.
	TestFalse(TEXT("a point above the panel is not covered"),
		FScreen::PanelCoversPoint(Panel,
			FVector2D(Panel.GetCenter().X, Panel.Min.Y - 1.0)));
	TestFalse(TEXT("a point below it is not"),
		FScreen::PanelCoversPoint(Panel,
			FVector2D(Panel.GetCenter().X, Panel.Max.Y + 1.0)));
	TestFalse(TEXT("a point left of it is not"),
		FScreen::PanelCoversPoint(Panel,
			FVector2D(Panel.Min.X - 1.0, Panel.GetCenter().Y)));
	TestFalse(TEXT("a point right of it is not"),
		FScreen::PanelCoversPoint(Panel,
			FVector2D(Panel.Max.X + 1.0, Panel.GetCenter().Y)));
	TestFalse(TEXT("the bottom left corner of the screen is not"),
		FScreen::PanelCoversPoint(Panel, FVector2D(0.0, 1080.0)));

	// AND A PANEL THAT IS NOT THERE COVERS NOTHING.
	TestFalse(TEXT("an invalid panel covers nothing"),
		FScreen::PanelCoversPoint(FBox2D(ForceInit), FVector2D(10.0, 10.0)));

	return true;
}

// ---------------------------------------------------------------------------
// Breaking a label into lines
// ---------------------------------------------------------------------------

/**
 * A label is wrapped on spaces, cut when it runs out of room, and says so.
 *
 * BY CHARACTER COUNT AND NOT BY MEASURED WIDTH, because no font exists under
 * -nullrhi and this has to be testable. ACataclysmHUD measures the line it is
 * about to draw and shrinks it when the count turns out to have been generous,
 * so the cost of the count being wrong is a smaller label rather than one that
 * spills over its frame.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryLabelLinesTest,
	"Cataclysm.InventoryScreen.ALabelIsWrappedOnSpacesAndCutWhenItRunsOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryLabelLinesTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	// A SHORT NAME IS ONE LINE AND IS NOT TOUCHED.
	{
		const TArray<FString> Lines =
			FScreen::LabelLinesFor(TEXT("Helm"), 12, 2);
		TestEqual(TEXT("'Helm' is one line"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("and reads unchanged"), Lines[0], FString(TEXT("Helm")));
		}
	}

	// TWO WORDS THAT FIT TOGETHER STAY TOGETHER, counting the space between.
	{
		const TArray<FString> Lines =
			FScreen::LabelLinesFor(TEXT("Corrupted Mote"), 20, 2);
		TestEqual(TEXT("'Corrupted Mote' fits on one line of 20"),
			Lines.Num(), 1);
	}

	// AND SPLIT WHEN THEY DO NOT.
	{
		const TArray<FString> Lines =
			FScreen::LabelLinesFor(TEXT("Corrupted Mote"), 12, 2);
		TestEqual(TEXT("'Corrupted Mote' takes two lines of 12"),
			Lines.Num(), 2);
		if (Lines.Num() == 2)
		{
			TestEqual(TEXT("the first word is whole"), Lines[0],
				FString(TEXT("Corrupted")));
			TestEqual(TEXT("and so is the second"), Lines[1],
				FString(TEXT("Mote")));
		}
	}

	// A THIRD WORD IS DROPPED AND THE LAST LINE SAYS SO, which is what tells a
	// player that a name was shortened rather than short.
	{
		const TArray<FString> Lines =
			FScreen::LabelLinesFor(TEXT("Jeweler's Setting Agent"), 12, 2);
		TestEqual(TEXT("three words take the two lines available"),
			Lines.Num(), 2);
		if (Lines.Num() == 2)
		{
			TestEqual(TEXT("the first line is whole"), Lines[0],
				FString(TEXT("Jeweler's")));
			TestTrue(TEXT("the second says the name went on"),
				Lines[1].EndsWith(FScreen::Ellipsis));
			TestTrue(TEXT("and still fits"), Lines[1].Len() <= 12);
		}
	}

	// A SINGLE WORD LONGER THAN A LINE IS CUT RATHER THAN BROKEN IN THE MIDDLE.
	// "Greatswo" then "rd" is worse to read than a shortened "Greatsw...".
	{
		const TArray<FString> Lines =
			FScreen::LabelLinesFor(TEXT("Greatsword"), 8, 2);
		TestEqual(TEXT("one long word takes one line"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("cut to the line's length"), Lines[0].Len(), 8);
			TestTrue(TEXT("and marked as cut"),
				Lines[0].EndsWith(FScreen::Ellipsis));
			TestTrue(TEXT("keeping the letters it had room for"),
				Lines[0].StartsWith(TEXT("Great")));
		}
	}

	// EVERY LINE FITS, WHATEVER IS ASKED FOR. This is the guarantee the drawing
	// leans on, so it is checked over the real names rather than over examples.
	{
		const TCHAR* Names[] = {
			TEXT("Helm"), TEXT("Two-Handed Crossbow"), TEXT("Greatsword"),
			TEXT("Crystal of Instability"), TEXT("Jeweler's Setting Agent"),
			TEXT("Schematic Fragments"), TEXT("Upgrade Stone (x)"),
			TEXT("Prismatic Catalyst"), TEXT("Purified Essence"),
		};

		for (const TCHAR* Name : Names)
		{
			for (int32 Width = 4; Width <= 20; ++Width)
			{
				const TArray<FString> Lines =
					FScreen::LabelLinesFor(Name, Width, 2);
				TestTrue(FString::Printf(
					TEXT("'%s' at %d takes no more than two lines"),
					Name, Width), Lines.Num() <= 2);

				for (const FString& Line : Lines)
				{
					if (Line.Len() > Width)
					{
						AddError(FString::Printf(
							TEXT("'%s' at %d produced a %d character line: %s"),
							Name, Width, Line.Len(), *Line));
					}
				}
			}
		}
	}

	// NOTHING TO SAY MAKES NO LINES, which is what an empty slot gets.
	TestEqual(TEXT("empty text makes no lines"),
		FScreen::LabelLinesFor(FString(), 12, 2).Num(), 0);
	TestEqual(TEXT("and neither does a line with no room"),
		FScreen::LabelLinesFor(TEXT("Helm"), 0, 2).Num(), 0);
	TestEqual(TEXT("nor a cell with no lines"),
		FScreen::LabelLinesFor(TEXT("Helm"), 12, 0).Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// What a cell says
// ---------------------------------------------------------------------------

/**
 * A cell is labelled with the base's name for gear and the material's for a
 * stack, and a stack of more than one carries its count.
 *
 * THE BASE'S NAME AND NOT THE ITEM'S. `Cataclysmic Greatsword of Malice` is not
 * going into a hundred pixels, and `Greatsword` is what an icon would say in a
 * game that had one. The rarity the full name opens with is already on the cell
 * twice, as the frame's colour and its thickness.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryCellLabelTest,
	"Cataclysm.InventoryScreen.ACellIsLabelledWithTheBaseOrTheMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryCellLabelTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
	if (!Bases || !Materials)
	{
		AddError(TEXT("DT_ItemBases or DT_CraftingMaterials does not exist. "
					  "Run tools/generate_datatable_assets.py."));
		return false;
	}

	TestEqual(TEXT("a gear slot is labelled with its base's name"),
		FScreen::LabelFor(GearSlot(TEXT("Head_Helm"), 1), Bases, Materials),
		FString(TEXT("Helm")));

	TestEqual(TEXT("a two-word base keeps both words"),
		FScreen::LabelFor(GearSlot(TEXT("Weapon_Greatsword"), 1), Bases,
						  Materials),
		FString(TEXT("Greatsword")));

	TestEqual(TEXT("a material slot is labelled with the material's name"),
		FScreen::LabelFor(MaterialSlot(TEXT("Material_Corrupted_Mote"), 34),
						  Bases, Materials),
		FString(TEXT("Corrupted Mote")));

	TestEqual(TEXT("an empty slot has no label"),
		FScreen::LabelFor(FCataclysmCarriedSlot(), Bases, Materials),
		FString());

	// A BASE THE TABLE DOES NOT HOLD HAS NO NAME, which is the same answer a
	// drop's name gives, rather than the row key showing through to the player.
	TestEqual(TEXT("a base the table does not hold has no label"),
		FScreen::LabelFor(GearSlot(TEXT("Not_A_Base"), 1), Bases, Materials),
		FString());
	TestEqual(TEXT("and nor does a material it does not hold"),
		FScreen::LabelFor(MaterialSlot(TEXT("Not_A_Material"), 3), Bases,
						  Materials),
		FString());

	// A MISSING TABLE IS SURVIVABLE, because both loaders can fail.
	TestEqual(TEXT("no base table means no label"),
		FScreen::LabelFor(GearSlot(TEXT("Head_Helm"), 1), nullptr, Materials),
		FString());
	TestEqual(TEXT("no material table means no label either"),
		FScreen::LabelFor(MaterialSlot(TEXT("Material_Corrupted_Mote"), 3),
						  Bases, nullptr),
		FString());

	// THE COUNT IS ONLY ON A STACK OF MORE THAN ONE. Gear does not stack, so a
	// count on it would always read 1 and mean nothing.
	TestEqual(TEXT("a stack of 34 says 34"),
		FScreen::QuantityTextFor(MaterialSlot(TEXT("Material_Corrupted_Mote"),
											  34)),
		FString(TEXT("34")));
	TestEqual(TEXT("a stack of one says nothing"),
		FScreen::QuantityTextFor(MaterialSlot(TEXT("Material_Corrupted_Mote"),
											  1)),
		FString());
	TestEqual(TEXT("a gear slot says nothing"),
		FScreen::QuantityTextFor(GearSlot(TEXT("Head_Helm"), 1)), FString());
	TestEqual(TEXT("an empty slot says nothing"),
		FScreen::QuantityTextFor(FCataclysmCarriedSlot()), FString());

	return true;
}

// ---------------------------------------------------------------------------
// The frame carries the rarity twice
// ---------------------------------------------------------------------------

/**
 * A frame's thickness is its rung, and it is the same ladder the drop names use.
 *
 * WHY A FRAME NEEDS ANYTHING BUT ITS COLOUR. The Interface Colour section of
 * `docs/Cataclysm_GDD_v2.md` requires that "the frame and the drop marker must
 * differ by shape or motion as well as by colour", because a player who cannot
 * separate two hues still has to separate two rarities and the ramp puts green,
 * yellow, orange and red on four adjacent rungs. That section names inventory
 * frames as one of the three surfaces the rarity ramp appears on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryFrameThicknessTest,
	"Cataclysm.InventoryScreen.AFrameThicknessIsItsRung",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryFrameThicknessTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
	if (!Materials)
	{
		AddError(TEXT("DT_CraftingMaterials does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	// EVERY RUNG, AND THE SAME ANSWER THE FLOOR GIVES. Written against
	// UCataclysmDropPickup rather than against numbers written here, because the
	// point is that the two agree; a second ladder is exactly what this avoids.
	for (int32 Step = 0; Step < UCataclysmDropRoll::RarityCount; ++Step)
	{
		const ECataclysmRarity Rarity = static_cast<ECataclysmRarity>(Step);
		const FCataclysmCarriedSlot Slot = SlotOfRarity(TEXT("Head_Helm"),
													   Rarity);

		TestEqual(FString::Printf(
			TEXT("rarity step %d carries the same thickness on a cell as on the floor"),
			Step),
			FScreen::BorderThicknessFor(Slot, Materials),
			UCataclysmDropPickup::NameBorderThicknessFor(Rarity));
	}

	// THE LADDER REALLY RISES, or every frame could be the same thickness and
	// the loop above would still pass.
	TestEqual(TEXT("Everyday sits inside one pixel"),
		FScreen::BorderThicknessFor(
			SlotOfRarity(TEXT("Head_Helm"), ECataclysmRarity::Everyday),
			Materials), 1);
	TestEqual(TEXT("Cataclysmic sits inside eight"),
		FScreen::BorderThicknessFor(
			SlotOfRarity(TEXT("Head_Helm"), ECataclysmRarity::Cataclysmic),
			Materials), 8);

	// A MATERIAL USES ITS TIER, ON THE SAME ONE-PIXEL-A-RUNG RULE.
	TestEqual(TEXT("a tier 1 material sits inside one pixel"),
		FScreen::BorderThicknessFor(
			MaterialSlot(TEXT("Material_Corrupted_Mote"), 5), Materials), 1);
	TestEqual(TEXT("a tier 5 material sits inside five"),
		FScreen::BorderThicknessFor(
			MaterialSlot(TEXT("Material_Purified_Essence"), 1), Materials), 5);

	// AN EMPTY SLOT STILL HAS A FRAME. The empty ones are what tell a player how
	// much room is left, so a cell drawn with nothing round it would read as a
	// fault in the drawing.
	TestEqual(TEXT("an empty slot gets the thinnest frame there is"),
		FScreen::BorderThicknessFor(FCataclysmCarriedSlot(), Materials),
		FScreen::EmptyCellBorderPx);
	TestTrue(TEXT("which is a frame rather than none"),
		FScreen::EmptyCellBorderPx > 0);

	return true;
}

/**
 * A frame's colour comes from the same two palettes the floor uses.
 *
 * IT IS THE SAME ITEM, so it is the same colour. The Interface Colour section
 * lists item names, inventory frames and the marker over a drop as the three
 * surfaces the gear rarity ramp appears on, and gives crafting materials a
 * separate ramp of five cyans that the gear ramp does not use.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryFrameColourTest,
	"Cataclysm.InventoryScreen.AFrameColourComesFromTheSamePaletteAsTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryFrameColourTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
	const UDataTable* Tiers = UCataclysmDropRoll::LoadMaterialTierTable();
	if (!Rarities || !Materials || !Tiers)
	{
		AddError(TEXT("DT_GearRarity, DT_CraftingMaterials or "
					  "DT_MaterialTiers does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	// EVERY RUNG MATCHES THE FLOOR'S ANSWER, and the eight are all different
	// from each other. Without the second half, a cell drawn white whatever it
	// held would pass the first.
	TSet<FString> Seen;
	for (int32 Step = 0; Step < UCataclysmDropRoll::RarityCount; ++Step)
	{
		const ECataclysmRarity Rarity = static_cast<ECataclysmRarity>(Step);
		const FLinearColor OnTheCell = FScreen::ColourFor(
			SlotOfRarity(TEXT("Head_Helm"), Rarity), Rarities, Materials, Tiers);
		const FLinearColor OnTheFloor =
			UCataclysmDropSpawner::ColourFor(Rarities, Rarity);

		TestTrue(FString::Printf(
			TEXT("rarity step %d is the same colour on a cell as on the floor"),
			Step), OnTheCell.Equals(OnTheFloor, 0.0001f));

		Seen.Add(OnTheCell.ToString());
	}
	TestEqual(TEXT("the eight rarities are eight different colours"),
		Seen.Num(), UCataclysmDropRoll::RarityCount);

	// A MATERIAL TAKES ITS TIER'S COLOUR, which is a different family.
	const FLinearColor Mote = FScreen::ColourFor(
		MaterialSlot(TEXT("Material_Corrupted_Mote"), 34), Rarities, Materials,
		Tiers);
	TestTrue(TEXT("a tier 1 material is its tier's colour"),
		Mote.Equals(UCataclysmDropRoll::MaterialColourFor(Tiers, 1), 0.0001f));

	const FLinearColor Essence = FScreen::ColourFor(
		MaterialSlot(TEXT("Material_Purified_Essence"), 1), Rarities, Materials,
		Tiers);
	TestTrue(TEXT("a tier 5 material is its tier's colour"),
		Essence.Equals(UCataclysmDropRoll::MaterialColourFor(Tiers, 5),
					   0.0001f));
	TestFalse(TEXT("and the two tiers are not the same colour"),
		Mote.Equals(Essence, 0.0001f));

	// AN EMPTY SLOT IS NOT ANY RARITY'S COLOUR, or an empty cell would read as
	// holding something.
	const FLinearColor Empty = FScreen::ColourFor(FCataclysmCarriedSlot(),
												 Rarities, Materials, Tiers);
	for (int32 Step = 0; Step < UCataclysmDropRoll::RarityCount; ++Step)
	{
		TestFalse(FString::Printf(
			TEXT("an empty cell is not rarity step %d's colour"), Step),
			Empty.Equals(UCataclysmDropSpawner::ColourFor(
				Rarities, static_cast<ECataclysmRarity>(Step)), 0.0001f));
	}

	return true;
}

// ---------------------------------------------------------------------------
// A material's tier, read back from its name
// ---------------------------------------------------------------------------

/**
 * A carried material knows which tier it is.
 *
 * THIS DID NOT EXIST UNTIL SOMETHING CARRIED A MATERIAL. RollMaterialTier picks
 * the tier and RollMaterial then picks a material inside it, so a drop never has
 * to ask; a carried slot stores only the material's name, so the screen does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialTierOfTest,
	"Cataclysm.DropRoll.AMaterialsTierCanBeReadFromItsName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialTierOfTest::RunTest(const FString&)
{
	const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
	if (!Materials)
	{
		AddError(TEXT("DT_CraftingMaterials does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	// ONE NAMED MATERIAL FROM EACH TIER, written here rather than read from the
	// table under test, so a table that lost a value fails rather than agreeing
	// with itself.
	struct FCase
	{
		const TCHAR* Row;
		int32 Tier;
	};

	const FCase Cases[] = {
		{ TEXT("Material_Corrupted_Mote"),   1 },
		{ TEXT("Material_Tainted_Shard"),    2 },
		{ TEXT("Material_Aetherial_Shard"),  3 },
		{ TEXT("Material_Focusing_Lens"),    4 },
		{ TEXT("Material_Purified_Essence"), 5 },
	};

	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%s is tier %d"), Case.Row, Case.Tier),
			UCataclysmDropRoll::MaterialTierOf(Materials, FName(Case.Row)),
			Case.Tier);
	}

	// EVERY MATERIAL THE ROLL CAN PRODUCE READS BACK AS THE TIER IT WAS ROLLED
	// FOR. This is the round trip the screen depends on, and it covers all
	// eighteen droppable materials rather than the five above.
	for (int32 Tier = 1; Tier <= 5; ++Tier)
	{
		FRandomStream Stream(Tier * 977);
		for (int32 Draw = 0; Draw < 40; ++Draw)
		{
			const FName Material =
				UCataclysmDropRoll::RollMaterial(Materials, Tier, Stream);
			if (Material.IsNone())
			{
				AddError(FString::Printf(TEXT("tier %d rolled no material"),
										 Tier));
				break;
			}

			const int32 ReadBack =
				UCataclysmDropRoll::MaterialTierOf(Materials, Material);
			if (ReadBack != Tier)
			{
				AddError(FString::Printf(
					TEXT("%s was rolled for tier %d and reads back as %d"),
					*Material.ToString(), Tier, ReadBack));
				break;
			}
		}
	}

	// A NAME THE TABLE DOES NOT HOLD HAS NO TIER, and neither does no table.
	TestEqual(TEXT("a material the table does not hold has no tier"),
		UCataclysmDropRoll::MaterialTierOf(Materials, TEXT("Not_A_Material")),
		0);
	TestEqual(TEXT("no name has no tier"),
		UCataclysmDropRoll::MaterialTierOf(Materials, NAME_None), 0);
	TestEqual(TEXT("no table has no tier"),
		UCataclysmDropRoll::MaterialTierOf(nullptr,
										   TEXT("Material_Corrupted_Mote")),
		0);

	// A CRAFTING ACTION IS NOT A MATERIAL AND CARRIES TIER 0, which is what
	// stops anything dropping "Reroll Affix Value".
	TestEqual(TEXT("a crafting action has no tier"),
		UCataclysmDropRoll::MaterialTierOf(Materials,
										   TEXT("Material_Reroll_Affix_Value")),
		0);

	return true;
}

// ---------------------------------------------------------------------------
// The header line
// ---------------------------------------------------------------------------

/**
 * The header says how many of the 48 slots are used.
 *
 * BOTH FIGURES RATHER THAN A BAR OR A FRACTION. The design makes a full bag a
 * choice about what to leave on the floor -- there is no way out of a dungeon
 * partway through -- so the number a player needs is how many slots are left.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryHeaderTest,
	"Cataclysm.InventoryScreen.TheHeaderSaysHowFullTheBagIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryHeaderTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	TestEqual(TEXT("an empty bag says 0 of 48"),
		FScreen::HeaderTextFor(0, UCataclysmInventoryComponent::SlotCount),
		FString(TEXT("Carried    0 / 48")));

	TestEqual(TEXT("a part-full bag says how many are used"),
		FScreen::HeaderTextFor(12, 48), FString(TEXT("Carried    12 / 48")));

	TestEqual(TEXT("a full bag says 48 of 48"),
		FScreen::HeaderTextFor(48, 48), FString(TEXT("Carried    48 / 48")));

	// THE CAPACITY IS PASSED IN RATHER THAN ASSUMED, so this line does not need
	// changing if the design ever moves the number the store holds.
	TestEqual(TEXT("a different capacity is printed as given"),
		FScreen::HeaderTextFor(3, 60), FString(TEXT("Carried    3 / 60")));

	return true;
}

// ---------------------------------------------------------------------------
// The label scales with the cell
// ---------------------------------------------------------------------------

/**
 * A cell's label shrinks with its cell, so the same words fit at every
 * resolution.
 *
 * WHY IT IS NOT A FIXED SIZE. The character count the wrapping uses is one
 * number for every viewport. A fixed text size would fit twelve characters
 * across a cell on a wide monitor and four on a small one, and the wrapping
 * would then have to be told the viewport as well.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryLabelScaleTest,
	"Cataclysm.InventoryScreen.ALabelScalesWithItsCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryLabelScaleTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	TestEqual(TEXT("a full-size cell uses the full label scale"),
		FScreen::LabelScaleFor(FScreen::MaxCellPx),
		FScreen::LabelScaleAtMaxCell);

	TestEqual(TEXT("a half-size cell uses half of it"),
		FScreen::LabelScaleFor(FScreen::MaxCellPx * 0.5f),
		FScreen::LabelScaleAtMaxCell * 0.5f);

	// IN PROPORTION MEANS THE RATIO IS THE SAME, which is the property the
	// character count leans on.
	const float Wide = FScreen::CellSizeFor(1920.0f, 1080.0f);
	const float Narrow = FScreen::CellSizeFor(1024.0f, 768.0f);
	TestTrue(TEXT("a narrow viewport really does draw smaller cells"),
		Narrow < Wide);
	TestTrue(TEXT("and its labels are smaller in the same proportion"),
		FMath::IsNearlyEqual(FScreen::LabelScaleFor(Narrow) / Narrow,
							 FScreen::LabelScaleFor(Wide) / Wide, 0.0001f));

	TestEqual(TEXT("a cell with no size has no label scale"),
		FScreen::LabelScaleFor(0.0f), 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
