// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmComparisonStandIn.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmGroundItemHover.h"
#include "Interface/CataclysmItemComparison.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Hovering an item on the ground shows the inventory's tooltip and its comparison with what is worn. Issue #2807, from
 * the owner's playtest of 2026-11-21: "I would like that same gear comparison thing we have in the inventory to pop up
 * when you hover over an item on the ground. That way you can tell if it's worth picking up before clogging your
 * inventory".
 *
 * THE SAME TEXT AS THE BAG, CHECKED AGAINST THE BAG'S OWN STEPS. The expected columns are built here the way
 * `UCataclysmInventoryWidget` built them before this change -- `UCataclysmItemComparison::Compare`, then `Columns`, or
 * `UCataclysmItemTooltip::TextFor` when nothing is compared -- and the ground hover must give exactly those. The bag now
 * reaches them through `UCataclysmItemComparison::TooltipColumnsFor`, the one function the hover uses too.
 *
 * WHAT IS NOT COVERED, said plainly: anything that reaches the screen. That the panel is drawn beside the cursor and
 * looks like the bag's is outside what a test here can see: the automation command passes -nullrhi, and issue #650
 * records that a widget does not move that wall. The bag's own cell tooltip cannot be built in a test world for the same
 * reason, so the comparison is with the steps the bag takes, not with the bag's widget. Where the panel sits and when it
 * is rebuilt are statics over plain values, and are tested.
 */
namespace CataclysmGroundItemHoverTest
{
	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* RingBase = TEXT("Ring_Band");
	const TCHAR* Material = TEXT("Material_Aetherial_Shard");

	FCataclysmItem PlainOf(const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		return Item;
	}

	FCataclysmItem WithHealthOf(const TCHAR* Base)
	{
		FCataclysmItem Item = PlainOf(Base);
		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(TEXT("Stat_Flat_maximum_health"));
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);
		return Item;
	}

	/** A character the bag compares against: the attribute sets a player holds, and equipment. */
	struct FWearer
	{
		explicit FWearer(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmPrimaryAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			Equipment = NewObject<UCataclysmEquipmentComponent>(Actor);
			Equipment->RegisterComponent();
		}

		void Wear(const FCataclysmItem& Item) const
		{
			FCataclysmItem First;
			FCataclysmItem Second;
			ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
			Equipment->Equip(Item, First, Second, Went);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/** The bag's columns for `Carried`, by the steps the bag took before this change. */
	bool TheBagsColumns(const FCataclysmCarriedSlot& Carried, FWearer& Wearer, ACataclysmComparisonStandIn& StandIn,
						FString& OutLeft, FString& OutRight)
	{
		const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
		const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
		const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
		const UDataTable* Positive = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
		const UDataTable* Negative = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
		FCataclysmComparisonResult Compared;
		if (!Carried.Item.Base.IsNone())
		{
			Compared = UCataclysmItemComparison::Compare(Carried.Item, *Wearer.Equipment, Wearer.AbilitySystem, StandIn);
		}
		OutRight.Reset();
		if (Compared.bCompared)
		{
			UCataclysmItemComparison::Columns(Carried, Compared, Bases, Affixes, Materials, Positive, Negative, OutLeft,
											  OutRight);
			return true;
		}
		OutLeft = UCataclysmItemTooltip::TextFor(Carried, Bases, Affixes, Materials, Positive, Negative);
		return false;
	}

	/** The world, a wearer and a stand-in, for one test. */
	struct FHoverScene
	{
		UWorld* World = nullptr;
		TUniquePtr<FWearer> Wearer;
		ACataclysmComparisonStandIn* StandIn = nullptr;

		bool IsUsable() const { return World && Wearer && StandIn; }
	};

	FHoverScene MakeScene()
	{
		FHoverScene Scene;
		Scene.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (Scene.World)
		{
			Scene.Wearer = MakeUnique<FWearer>(Scene.World);
			Scene.StandIn = ACataclysmComparisonStandIn::SpawnIn(Scene.World);
		}
		return Scene;
	}

	ACataclysmDroppedItem* GearOnTheFloor(UWorld* World, const FCataclysmItem& Item)
	{
		return UCataclysmDropSpawner::PutOnTheFloor(World, FVector(300.0f, 0.0f, 0.0f), Item, NAME_None, 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundHelmComparedTest,
	"Cataclysm.GroundItemHover.AHelmOnTheFloorShowsTheBagsComparisonWithTheWornHelm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A wearer in a plain helm, a helm with health on the floor: the hover's two columns are the bag's, word for word. */
bool FCataclysmGroundHelmComparedTest::RunTest(const FString&)
{
	using namespace CataclysmGroundItemHoverTest;
	FHoverScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: a world, a wearer and a stand-in"), Scene.IsUsable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	Scene.Wearer->Wear(PlainOf(HelmBase));

	ACataclysmDroppedItem* Drop = GearOnTheFloor(Scene.World, WithHealthOf(HelmBase));
	if (!TestNotNull(TEXT("set-up: a helm on the floor"), Drop))
	{
		return false;
	}
	TestEqual(TEXT("it is hovered as gear"), static_cast<int32>(UCataclysmGroundItemHover::KindFor(Drop)),
			  static_cast<int32>(ECataclysmGroundHover::Gear));

	FString BagLeft;
	FString BagRight;
	FCataclysmCarriedSlot Carried;
	Carried.Item = Drop->Item;
	TestTrue(TEXT("set-up: the bag compares it"),
			 TheBagsColumns(Carried, *Scene.Wearer, *Scene.StandIn, BagLeft, BagRight));

	FString Left;
	FString Right;
	TestTrue(TEXT("the hover shows something"),
			 UCataclysmGroundItemHover::ColumnsFor(Drop, Scene.Wearer->Equipment, Scene.Wearer->AbilitySystem,
												   Scene.StandIn, Left, Right));
	TestEqual(TEXT("its left column is the bag's"), Left, BagLeft);
	TestEqual(TEXT("its right column is the bag's"), Right, BagRight);
	TestFalse(TEXT("and the right column is not empty: it compares"), Right.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundRingEmptySlotTest,
	"Cataclysm.GroundItemHover.ARingForAnEmptySlotShowsComparedWithNothingAsTheBagDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * NO RING WORN: the bag still compares, its right column reading "Compared with nothing", and so does the hover. The
 * coordinating session's ruling Q2 of 2026-11-21, which corrected the issue's "no equipped counterpart shows no
 * comparison".
 */
bool FCataclysmGroundRingEmptySlotTest::RunTest(const FString&)
{
	using namespace CataclysmGroundItemHoverTest;
	FHoverScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: a world, a wearer and a stand-in"), Scene.IsUsable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDroppedItem* Drop = GearOnTheFloor(Scene.World, WithHealthOf(RingBase));
	if (!TestNotNull(TEXT("set-up: a ring on the floor"), Drop))
	{
		return false;
	}
	FString BagLeft;
	FString BagRight;
	FCataclysmCarriedSlot Carried;
	Carried.Item = Drop->Item;
	TheBagsColumns(Carried, *Scene.Wearer, *Scene.StandIn, BagLeft, BagRight);

	FString Left;
	FString Right;
	UCataclysmGroundItemHover::ColumnsFor(Drop, Scene.Wearer->Equipment, Scene.Wearer->AbilitySystem, Scene.StandIn,
										  Left, Right);
	TestTrue(TEXT("the bag's right column says it is compared with nothing"),
			 BagRight.Contains(TEXT("Compared with nothing")));
	TestEqual(TEXT("the hover's left column is the bag's"), Left, BagLeft);
	TestEqual(TEXT("and its right column, compared with nothing"), Right, BagRight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundMaterialTest,
	"Cataclysm.GroundItemHover.AMaterialOnTheFloorShowsItsOwnLinesAndNoComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL WITH NO COMPARISON: a crafting material, whose lines are the bag's for the same stack. */
bool FCataclysmGroundMaterialTest::RunTest(const FString&)
{
	using namespace CataclysmGroundItemHoverTest;
	FHoverScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: a world, a wearer and a stand-in"), Scene.IsUsable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	Scene.Wearer->Wear(PlainOf(HelmBase));

	ACataclysmDroppedItem* Drop = UCataclysmDropSpawner::PutOnTheFloor(Scene.World, FVector(300.0f, 0.0f, 0.0f),
																		  FCataclysmItem(), FName(Material), 3);
	if (!TestNotNull(TEXT("set-up: three shards on the floor"), Drop))
	{
		return false;
	}
	TestEqual(TEXT("it is hovered as a material"), static_cast<int32>(UCataclysmGroundItemHover::KindFor(Drop)),
			  static_cast<int32>(ECataclysmGroundHover::Material));

	FCataclysmCarriedSlot Carried;
	Carried.Material = FName(Material);
	Carried.Quantity = 3;
	FString BagLeft;
	FString BagRight;
	TestFalse(TEXT("set-up: the bag compares nothing for it"),
			  TheBagsColumns(Carried, *Scene.Wearer, *Scene.StandIn, BagLeft, BagRight));

	FString Left;
	FString Right;
	TestTrue(TEXT("the hover shows something"),
			 UCataclysmGroundItemHover::ColumnsFor(Drop, Scene.Wearer->Equipment, Scene.Wearer->AbilitySystem,
												   Scene.StandIn, Left, Right));
	TestEqual(TEXT("its own lines, the bag's"), Left, BagLeft);
	TestTrue(TEXT("and no comparison"), Right.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundNothingTest,
	"Cataclysm.GroundItemHover.NothingHoveredAndAGoldPileShowNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL WITH NOTHING: no drop under the cursor, and a gold pile, whose tag already states its amount. */
bool FCataclysmGroundNothingTest::RunTest(const FString&)
{
	using namespace CataclysmGroundItemHoverTest;
	FHoverScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: a world, a wearer and a stand-in"), Scene.IsUsable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FString Left = TEXT("stale");
	FString Right = TEXT("stale");
	TestEqual(TEXT("nothing hovered is nothing"), static_cast<int32>(UCataclysmGroundItemHover::KindFor(nullptr)),
			  static_cast<int32>(ECataclysmGroundHover::Nothing));
	TestFalse(TEXT("and shows nothing"),
			  UCataclysmGroundItemHover::ColumnsFor(nullptr, Scene.Wearer->Equipment, Scene.Wearer->AbilitySystem,
													Scene.StandIn, Left, Right));
	TestTrue(TEXT("with both columns emptied"), Left.IsEmpty() && Right.IsEmpty());

	ACataclysmDroppedItem* Gold = UCataclysmDropSpawner::PutOnTheFloor(Scene.World, FVector(300.0f, 0.0f, 0.0f),
																		  FCataclysmItem(), NAME_None, 0, /*Gold=*/25);
	if (!TestNotNull(TEXT("set-up: a gold pile"), Gold) || !TestTrue(TEXT("set-up: it is gold"), Gold->IsGold()))
	{
		return false;
	}
	TestEqual(TEXT("a gold pile is nothing to hover"), static_cast<int32>(UCataclysmGroundItemHover::KindFor(Gold)),
			  static_cast<int32>(ECataclysmGroundHover::Nothing));
	TestFalse(TEXT("and shows nothing"),
			  UCataclysmGroundItemHover::ColumnsFor(Gold, Scene.Wearer->Equipment, Scene.Wearer->AbilitySystem,
													Scene.StandIn, Left, Right));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundPanelPlacementTest,
	"Cataclysm.GroundItemHover.ThePanelSitsBelowRightOfTheCursorAndStaysOnScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Where the panel goes, as a static over plain numbers: beside the cursor, and turned back at an edge. */
bool FCataclysmGroundPanelPlacementTest::RunTest(const FString&)
{
	using Hover = UCataclysmGroundItemHover;
	const FVector2D Panel(300.0f, 200.0f);
	const FVector2D Viewport(1920.0f, 1080.0f);
	const float Offset = Hover::CursorOffsetPx;

	TestEqual(TEXT("with room, below and to the right of the cursor"),
			  Hover::PanelPositionFor(FVector2D(100.0f, 100.0f), Panel, Viewport),
			  FVector2D(100.0f + Offset, 100.0f + Offset));
	TestEqual(TEXT("near the right edge, to the left of the cursor instead"),
			  Hover::PanelPositionFor(FVector2D(1800.0f, 100.0f), Panel, Viewport),
			  FVector2D(1800.0f - Offset - Panel.X, 100.0f + Offset));
	TestEqual(TEXT("near the bottom edge, above the cursor instead"),
			  Hover::PanelPositionFor(FVector2D(100.0f, 1000.0f), Panel, Viewport),
			  FVector2D(100.0f + Offset, 1000.0f - Offset - Panel.Y));
	TestEqual(TEXT("a panel larger than the room left is held at the screen's top left"),
			  Hover::PanelPositionFor(FVector2D(10.0f, 10.0f), FVector2D(4000.0f, 3000.0f), Viewport),
			  FVector2D(0.0f, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundRebuildTest,
	"Cataclysm.GroundItemHover.ThePanelIsRebuiltOnlyWhenTheDropOrTheWornGearChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE HOVER MUST NOT COST THE FRAME, the coordinating session's condition of 2026-11-21: the text is worked out again
 * only when the drop under the cursor or what is worn has changed. THE CONTROL is the same drop with the same worn gear,
 * which is not rebuilt.
 */
bool FCataclysmGroundRebuildTest::RunTest(const FString&)
{
	using namespace CataclysmGroundItemHoverTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmDroppedItem* One = GearOnTheFloor(World, PlainOf(HelmBase));
	ACataclysmDroppedItem* Two = GearOnTheFloor(World, PlainOf(RingBase));
	if (!TestNotNull(TEXT("set-up: one drop"), One) || !TestNotNull(TEXT("set-up: and another"), Two))
	{
		return false;
	}
	using Hover = UCataclysmGroundItemHover;
	TestFalse(TEXT("control: the same drop and the same worn gear is not rebuilt"),
			  Hover::NeedsRebuild(One, One, /*WornChanges=*/4, /*ShownAtWornChanges=*/4));
	TestTrue(TEXT("another drop is"), Hover::NeedsRebuild(Two, One, 4, 4));
	TestTrue(TEXT("a change to what is worn is"), Hover::NeedsRebuild(One, One, 5, 4));
	TestTrue(TEXT("a drop where nothing was shown is"), Hover::NeedsRebuild(One, nullptr, 4, 4));
	TestTrue(TEXT("nothing where a drop was shown is"), Hover::NeedsRebuild(nullptr, One, 4, 4));
	TestFalse(TEXT("control: nothing, still nothing, is not"), Hover::NeedsRebuild(nullptr, nullptr, 4, 4));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
