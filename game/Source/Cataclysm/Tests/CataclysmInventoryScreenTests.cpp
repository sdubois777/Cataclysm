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
 * Tests for the carried inventory screen. Issues #731 and #735.
 *
 * WHAT IS COVERED. Every judgement UCataclysmInventoryScreen makes: how big a
 * cell is for a viewport, what a slot is labelled, how many are stacked in it,
 * what colour and how thick its frame is, what size its label is drawn at, and
 * what the header line reads.
 *
 * WHAT IS THEREFORE NOT COVERED, said plainly: anything that reaches the screen.
 * That UCataclysmInventoryWidget builds the tree it means to, that the panel is
 * legible, that pressing I adds it to the viewport, and that a click on it
 * really stops being a move order are all outside what a test here can see. The
 * automation command in tools/unreal_build.py passes -nullrhi, and issue #650
 * records that a widget does not move that wall. Those four were checked by
 * playing.
 *
 * WHAT THESE TESTS LOST WHEN THE SCREEN BECAME A WIDGET, issue #735. Where the
 * panel sat, where each of the 48 cells sat, and how a long label broke into
 * lines were all tested here, because a canvas draw has no layout and the screen
 * had to work them out. Slate does all three, so the functions are gone and so
 * are their tests. What replaced them is one assertion below: that a cell size
 * really does leave twelve cells and their padding fitting the viewport.
 */
namespace CataclysmInventoryScreenTest
{
	using FScreen = UCataclysmInventoryScreen;

	/** A slot holding a piece of gear. */
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

	/** A slot holding gear that really is this rarity. */
	FCataclysmCarriedSlot SlotOfRarity(const TCHAR* Base,
									   ECataclysmRarity Rarity)
	{
		return GearSlot(Base, UCataclysmItemValues::AffixSlotsFor(Rarity),
						UCataclysmItemValues::EnchantmentsFor(Rarity));
	}

	/** Every viewport shape the cell size has to hold for. */
	struct FViewportShape
	{
		float Width;
		float Height;
	};

	// Sixteen by nine, sixteen by ten, four by three, a tall window, an
	// ultra-wide monitor, and a viewport small enough to be silly.
	const FViewportShape Viewports[] = {
		{ 3840.0f, 2160.0f }, { 2560.0f, 1440.0f }, { 1920.0f, 1080.0f },
		{ 1660.0f,  750.0f }, { 1600.0f, 1000.0f }, { 1280.0f,  720.0f },
		{ 1024.0f,  768.0f }, {  800.0f,  600.0f }, {  720.0f, 1280.0f },
		{ 3440.0f, 1440.0f }, {  400.0f,  300.0f },
	};
}

// ---------------------------------------------------------------------------
// The grid fits on the screen
// ---------------------------------------------------------------------------

/**
 * Twelve cells and their padding fit inside every viewport shape.
 *
 * THIS IS THE ONE LAYOUT RULE SLATE CANNOT WORK OUT, and it is why CellSizeFor
 * survived the port to a widget. A UUniformGridPanel makes every cell the same
 * size and sizes them from their contents, so one long item name would widen all
 * 48 and push the last column past the edge of the screen, where it cannot be
 * read and nothing says why. Each cell is given an exact size instead, and this
 * is the guarantee that size carries.
 *
 * THE GAP IS COUNTED TWELVE TIMES AND NOT ELEVEN. The widget gives every cell
 * half a gap on each of its four sides, which puts a whole gap between two of
 * them and also half a gap outside the first and the last. Asserting the larger
 * figure is what makes this true of the widget rather than of the arithmetic.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryCellSizeFitsTest,
	"Cataclysm.InventoryScreen.TwelveCellsFitInsideEveryViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryCellSizeFitsTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	const int32 Columns = UCataclysmInventoryComponent::Columns;
	const int32 Rows = UCataclysmInventoryComponent::Rows;

	for (const FViewportShape& Viewport : Viewports)
	{
		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"),
											  Viewport.Width, Viewport.Height);

		const float CellPx = FScreen::CellSizeFor(Viewport.Width,
												  Viewport.Height);

		TestTrue(*(Where + TEXT(": a cell has a positive size")),
			CellPx > 0.0f);
		TestTrue(*(Where + TEXT(": and is no bigger than the ceiling")),
			CellPx <= FScreen::MaxCellPx);

		const float PanelWidth = CellPx * Columns
			+ FScreen::CellGapPx * Columns + FScreen::PanelPaddingPx * 2.0f;
		const float PanelHeight = FScreen::HeaderHeightPx + CellPx * Rows
			+ FScreen::CellGapPx * Rows + FScreen::PanelPaddingPx * 2.0f;

		TestTrue(*(Where + TEXT(": twelve cells fit across the viewport")),
			PanelWidth <= Viewport.Width);
		TestTrue(*(Where + TEXT(": four rows and the header fit down it")),
			PanelHeight <= Viewport.Height);
	}

	// THE CEILING REALLY BINDS somewhere, or it is not a ceiling. A very wide
	// viewport has room for far more than MaxCellPx and must not use it.
	TestEqual(TEXT("a huge viewport draws cells at the ceiling size"),
		FScreen::CellSizeFor(7680.0f, 4320.0f), FScreen::MaxCellPx);

	// AND THE VIEWPORT REALLY BINDS somewhere too, or the ceiling is the only
	// rule and the fitting is untested.
	TestTrue(TEXT("a narrow viewport draws them smaller than the ceiling"),
		FScreen::CellSizeFor(800.0f, 600.0f) < FScreen::MaxCellPx);

	// A CELL IS NEVER ZERO OR NEGATIVE, whatever it is asked. Slate treats a
	// negative size override as an error rather than as a very small box.
	TestTrue(TEXT("a viewport of nothing still gives a positive cell"),
		FScreen::CellSizeFor(0.0f, 0.0f) > 0.0f);
	TestTrue(TEXT("and so does a viewport too small to hold the padding"),
		FScreen::CellSizeFor(50.0f, 40.0f) > 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// The label's size follows its cell
// ---------------------------------------------------------------------------

/**
 * A cell's label is drawn at a size in proportion to its cell.
 *
 * WHY IT IS NOT A FIXED SIZE. A fixed size would fit a whole item name across a
 * cell on a wide monitor and two letters on a small one, and the grid would read
 * differently at every resolution.
 *
 * AND WHY THERE IS A FLOOR. Below a few points a font stops being text, and a
 * viewport small enough to reach that is better served by a label that overflows
 * its cell than by one that cannot be read at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryLabelFontTest,
	"Cataclysm.InventoryScreen.ALabelsSizeFollowsItsCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryLabelFontTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryScreenTest;

	TestEqual(TEXT("a full-size cell takes its share of the cell's height"),
		FScreen::LabelFontSizeFor(FScreen::MaxCellPx),
		FMath::RoundToInt(FScreen::MaxCellPx * FScreen::LabelFontShareOfCell));

	// A SMALLER CELL REALLY DOES GET A SMALLER FONT, or the proportion is not
	// doing anything.
	TestTrue(TEXT("half a cell gets a smaller font than a whole one"),
		FScreen::LabelFontSizeFor(FScreen::MaxCellPx * 0.5f)
			< FScreen::LabelFontSizeFor(FScreen::MaxCellPx));

	// IT NEVER RISES AS THE CELL SHRINKS, checked across the range rather than
	// at two points.
	int32 Previous = FScreen::LabelFontSizeFor(1.0f);
	for (float CellPx = 1.0f; CellPx <= FScreen::MaxCellPx; CellPx += 1.0f)
	{
		const int32 Sized = FScreen::LabelFontSizeFor(CellPx);
		if (Sized < Previous)
		{
			AddError(FString::Printf(
				TEXT("a %.0f pixel cell gets font %d, smaller than the %d a "
					 "narrower one got"), CellPx, Sized, Previous));
			break;
		}
		Previous = Sized;
	}

	// THE FLOOR HOLDS, and a cell of nothing does not ask Slate for a font of
	// nothing.
	TestEqual(TEXT("a tiny cell still gets the smallest readable font"),
		FScreen::LabelFontSizeFor(1.0f), FScreen::SmallestLabelFontPx);
	TestEqual(TEXT("and so does a cell of no size at all"),
		FScreen::LabelFontSizeFor(0.0f), FScreen::SmallestLabelFontPx);
	TestEqual(TEXT("and a negative one, which Slate would refuse"),
		FScreen::LabelFontSizeFor(-40.0f), FScreen::SmallestLabelFontPx);

	// THE HEADER IS BIGGER THAN A CELL'S LABEL at the largest cell, because it
	// is the one line a player reads first.
	TestTrue(TEXT("the header is larger than a cell's label"),
		FScreen::HeaderFontPx > FScreen::LabelFontSizeFor(FScreen::MaxCellPx));

	// A STACK COUNT IS BIGGER THAN THE LABEL BESIDE IT, since issue #734. How
	// many of a material are carried is one of the two things this screen
	// exists to show, and at the label's size the figure sat in a corner small
	// enough to miss. Checked across the range, not at one cell size, because
	// two proportions can cross over.
	for (float CellPx = 1.0f; CellPx <= FScreen::MaxCellPx; CellPx += 1.0f)
	{
		const int32 Label = FScreen::LabelFontSizeFor(CellPx);
		const int32 Quantity = FScreen::QuantityFontSizeFor(CellPx);
		if (Quantity < Label)
		{
			AddError(FString::Printf(
				TEXT("a %.0f pixel cell draws its count at %d and its label at "
					 "%d, so the count is the smaller of the two"),
				CellPx, Quantity, Label));
			break;
		}
	}

	TestTrue(TEXT("and really is bigger at a full-size cell"),
		FScreen::QuantityFontSizeFor(FScreen::MaxCellPx)
			> FScreen::LabelFontSizeFor(FScreen::MaxCellPx));

	// IT SHARES THE LABEL'S FLOOR, so a cell too small for a readable label does
	// not get a readable count either, which would be the wrong way round.
	TestEqual(TEXT("a tiny cell's count stops at the smallest readable font"),
		FScreen::QuantityFontSizeFor(1.0f), FScreen::SmallestLabelFontPx);
	TestEqual(TEXT("and so does a cell of no size at all"),
		FScreen::QuantityFontSizeFor(0.0f), FScreen::SmallestLabelFontPx);

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
 * twice, as the frame's colour and its thickness. Issue #733 is the tooltip that
 * carries the rest.
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
		FScreen::LabelFor(GearSlot(TEXT("Weapon_Two_Handed_Crossbow"), 1),
						  Bases, Materials),
		FString(TEXT("Two-Handed Crossbow")));

	// A THREE-WORD MATERIAL NAME IS KEPT WHOLE. The canvas version cut it to two
	// lines of twelve characters and lost the last word; the widget wraps by
	// measured width and cuts with an ellipsis only when it has to, so nothing
	// here has to shorten anything. Issue #735.
	TestEqual(TEXT("a material slot is labelled with the material's whole name"),
		FScreen::LabelFor(
			MaterialSlot(TEXT("Material_Jeweler_s_Setting_Agent"), 3),
			Bases, Materials),
		FString(TEXT("Jeweler's Setting Agent")));

	TestEqual(TEXT("and a two-word one likewise"),
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
 * frames as a surface the rarity ramp appears on.
 *
 * THE THICKNESS IS THE PADDING BETWEEN TWO FILLED RECTANGLES in the widget, so
 * this figure is a real number of pixels there as it was on the canvas.
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

	// AND NO FRAME IS THICK ENOUGH TO SWALLOW ITS OWN CELL, AT ANY WINDOW.
	// The frame is drawn as padding inside the cell, so a thickness at or
	// above half the cell's size leaves no interior for the label.
	//
	// IT USED TO CHECK ONE WINDOW AND THAT WAS NOT ENOUGH. Adding the three
	// columns of the worn gear panel in issue #831 made a 400 by 300 window
	// produce a cell of 14.9 against a thickest frame of 8, and only that
	// one window was being asked. The floor UCataclysmInventoryScreen::
	// SmallestCellPx now puts under the cell is what makes the rule hold
	// everywhere, so the test asks everywhere rather than somewhere.
	const int32 Thickest = FMath::Max(
		FScreen::BorderThicknessFor(
			MaterialSlot(TEXT("Material_Purified_Essence"), 1), Materials),
		FScreen::EmptyCellBorderPx);

	for (const FVector2D Viewport : {FVector2D(3840.0, 2160.0),
									 FVector2D(1920.0, 1080.0),
									 FVector2D(1024.0, 768.0),
									 FVector2D(400.0, 300.0),
									 FVector2D(1.0, 1.0)})
	{
		const float Cell = FScreen::CellSizeFor(
			static_cast<float>(Viewport.X), static_cast<float>(Viewport.Y));
		TestTrue(FString::Printf(
			TEXT("a %.0f by %.0f window leaves an interior: cell %.1f against "
				 "a thickest frame of %d each side"),
			Viewport.X, Viewport.Y, Cell, Thickest),
			static_cast<float>(Thickest) * 2.0f < Cell);
	}

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

#endif // WITH_AUTOMATION_TESTS
