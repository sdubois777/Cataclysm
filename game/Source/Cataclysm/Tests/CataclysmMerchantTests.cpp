// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Interface/CataclysmEmpireMapWidget.h"
#include "Interface/CataclysmMerchantWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmMerchant.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The merchant, issue #48, slice S1: what a carried piece sells for, the sale, and the Merchant screen.
 *
 * WHAT IS NOT COVERED, said plainly: the drawing, and a click reaching `Press`. A headless test has no Widget Blueprint,
 * so the screen's buttons never exist; the tests press the commands the buttons carry and read `ButtonsShown`.
 * `tools/tests/test_the_merchant_is_wired_in.py` holds that the map's button and the screen's Close reach the controller.
 */
namespace CataclysmMerchantTest
{
	using FMerchant = UCataclysmMerchant;

	/** A piece of a rarity at an upgrade level: as many affixes and enchantments as the rarity is made of. */
	FCataclysmItem Piece(ECataclysmRarity Rarity, int32 GearLevel, const TCHAR* Base = TEXT("Gloves_Handwraps"))
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		Item.EnchantmentCount = UCataclysmItemValues::EnchantmentsFor(Rarity);
		for (int32 Index = 0; Index < UCataclysmItemValues::AffixSlotsFor(Rarity); ++Index)
		{
			FCataclysmRolledAffix Rolled;
			Rolled.Affix = FName(*FString::Printf(TEXT("Affix_%d"), Index));
			Item.Affixes.Add(Rolled);
		}
		return Item;
	}

	struct FMerchantScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && State && Character && Character->GetInventory() && Character->GetEquipment()
				&& Run && Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }
	};

	/** A world with a possessed Solo Self-Found player holding `Gold` of its own and a tier 1 run; nothing carried. */
	FMerchantScene MakeScene(int64 Gold)
	{
		FMerchantScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
			FCataclysmCharacterPartition Partition;
			Partition.Lethality = ECataclysmLethality::Standard;
			Partition.Population = ECataclysmPopulation::Offline;
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Hawker"), Partition);
			Out.State->SetOwnGold(Gold);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 48, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}
}

// ---------------------------------------------------------------------------
// M1. The price: doubling a rung, 10% a level, in piles, half up, at least one
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMerchantPriceTest,
	"Cataclysm.Merchant.ThePriceDoublesByRarityAndRisesTenPercentALevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Against a 10-gold pile (tier 1's): Everyday +0 is 0.625, so 1; Superb +0 is 2.5, half up to 3; Masterful +0 is 5;
 * Legendary +4 is 10 x 1 x 1.4 = 14; Cataclysmic +10 is 10 x 8 x 2 = 160. Against 300 (tier 8's): Everyday +0 is 18.75,
 * so 19; Masterful +3 is 300 x 0.5 x 1.3 = 195. A piece that is no rarity, and a pile of nothing, sell for nothing.
 */
bool FCataclysmMerchantPriceTest::RunTest(const FString&)
{
	using namespace CataclysmMerchantTest;
	TestEqual(TEXT("**Everyday +0 at 10: 0.625, at least one**"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Everyday, 0), 10.0), static_cast<int64>(1));
	TestEqual(TEXT("**Superb +0 at 10: 2.5, half up to 3**"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Superb, 0), 10.0), static_cast<int64>(3));
	TestEqual(TEXT("Masterful +0 at 10: 5"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Masterful, 0), 10.0), static_cast<int64>(5));
	TestEqual(TEXT("**Legendary +4 at 10: 14**"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Legendary, 4), 10.0), static_cast<int64>(14));
	TestEqual(TEXT("**Cataclysmic +10 at 10: 160**"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Cataclysmic, 10), 10.0), static_cast<int64>(160));
	TestEqual(TEXT("Everyday +0 at 300: 18.75, so 19"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Everyday, 0), 300.0), static_cast<int64>(19));
	TestEqual(TEXT("**Masterful +3 at 300: 195**"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Masterful, 3), 300.0), static_cast<int64>(195));

	FCataclysmItem Odd = Piece(ECataclysmRarity::Everyday, 0);
	Odd.EnchantmentCount = 1;
	TestEqual(TEXT("**an enchantment with empty slots is no rarity: nothing**"), FMerchant::SellValueOf(Odd, 10.0), static_cast<int64>(0));
	TestEqual(TEXT("a pile of nothing: nothing"), FMerchant::SellValueOf(Piece(ECataclysmRarity::Superb, 0), 0.0), static_cast<int64>(0));
	TestEqual(TEXT("the table holds eight rungs"), FMerchant::RarityCount, 8);
	return true;
}

// ---------------------------------------------------------------------------
// M2. Counted in the tier's pile, with no floor's bonus
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMerchantTierTest,
	"Cataclysm.Merchant.ThePriceIsCountedInTheTiersGoldPile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The gold table's piles, 10 at tier 1 and 300 at tier 8; a Masterful +0 sells for 5 and 150 there. */
bool FCataclysmMerchantTierTest::RunTest(const FString&)
{
	using namespace CataclysmMerchantTest;
	TestEqual(TEXT("**tier 1's pile is 10**"), FMerchant::PileGoldAt(1), 10.0, 0.001);
	TestEqual(TEXT("**tier 8's pile is 300**"), FMerchant::PileGoldAt(8), 300.0, 0.001);
	TestEqual(TEXT("a Masterful +0 at tier 1: 5"), FMerchant::SellValueAt(Piece(ECataclysmRarity::Masterful, 0), 1), static_cast<int64>(5));
	TestEqual(TEXT("**and at tier 8: 150, thirty times**"), FMerchant::SellValueAt(Piece(ECataclysmRarity::Masterful, 0), 8), static_cast<int64>(150));
	return true;
}

// ---------------------------------------------------------------------------
// M3. A sale banks the price and empties the cell; anything else is refused
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMerchantSaleTest,
	"Cataclysm.Merchant.ASaleBanksThePriceAndEmptiesTheCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Solo Self-Found player with 100 gold sells a Masterful +3 at tier 1: 6.5, half up to 7, so 107 held and the cell
 * empty, with no day passed. Selling the empty cell, a material's cell and a cell outside the bag is refused and
 * changes nothing. A worn piece is never among the pieces offered.
 */
bool FCataclysmMerchantSaleTest::RunTest(const FString&)
{
	using namespace CataclysmMerchantTest;
	FMerchantScene Scene = MakeScene(100);
	ON_SCOPE_EXIT { if (Scene.World) { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); } };
	if (!TestTrue(TEXT("set-up: a world, a Solo Self-Found player with 100 gold and a run"), Scene.IsUsable()))
	{
		return false;
	}
	const int32 Cell = Scene.Bag()->AddItem(Piece(ECataclysmRarity::Masterful, 3));
	const int32 MaterialCell = Scene.Bag()->AddMaterial(FName(TEXT("Material_Corrupted_Mote")), 2);
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Scene.Character->GetEquipment()->EquipInto(Piece(ECataclysmRarity::Superb, 0, TEXT("Head_Helm")), ECataclysmGearSlot::Head,
											   Removed, AlsoRemoved);
	TestTrue(TEXT("**only the carried piece is offered, not the worn helm nor the material**"),
			 FMerchant::CarriedPiecesOf(Scene.Character) == TArray<int32>({Cell}));

	const int32 DayBefore = Scene.Run->Day();
	int64 Gold = 0;
	FString Message;
	TestTrue(TEXT("the sale is made"), FMerchant::Sell(Scene.World, Cell, Gold, Message));
	TestEqual(TEXT("**it paid 7**"), Gold, static_cast<int64>(7));
	TestEqual(TEXT("**107 held**"), Scene.State->GetOwnGold(), static_cast<int64>(107));
	TestNull(TEXT("**the cell is empty**"), Scene.Bag()->ItemAt(Cell));
	TestEqual(TEXT("**no day passed**"), Scene.Run->Day(), DayBefore);
	TestTrue(TEXT("the message names the sale"), Message.StartsWith(TEXT("Sold ")) && Message.EndsWith(TEXT(" for 7 gold.")));

	TestFalse(TEXT("**the empty cell is refused**"), FMerchant::Sell(Scene.World, Cell, Gold, Message));
	TestEqual(TEXT("with its sentence"), Message, FString(FMerchant::NoItemRefusal));
	TestFalse(TEXT("**a material's cell is refused**"), FMerchant::Sell(Scene.World, MaterialCell, Gold, Message));
	TestFalse(TEXT("a cell outside the bag is refused"), FMerchant::Sell(Scene.World, 99, Gold, Message));
	TestEqual(TEXT("**and nothing more was paid**"), Scene.State->GetOwnGold(), static_cast<int64>(107));
	TestEqual(TEXT("the material is still carried"), Scene.Bag()->CountOfMaterial(FName(TEXT("Material_Corrupted_Mote"))), 2);
	return true;
}

// ---------------------------------------------------------------------------
// M4. Not during a walk
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMerchantWalkTest,
	"Cataclysm.Merchant.ASaleIsRefusedDuringAWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMerchantWalkTest::RunTest(const FString&)
{
	using namespace CataclysmMerchantTest;
	FMerchantScene Scene = MakeScene(100);
	ON_SCOPE_EXIT { if (Scene.World) { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); } };
	if (!TestTrue(TEXT("set-up: the scene"), Scene.IsUsable()))
	{
		return false;
	}
	const int32 Cell = Scene.Bag()->AddItem(Piece(ECataclysmRarity::Quality, 0));
	Scene.Run->Clock->EnterDungeon(/*DungeonId*/ 7);
	TestTrue(TEXT("CONTROL: a walk is under way"), Scene.Run->IsAWalkUnderWay());
	int64 Gold = 0;
	FString Message;
	TestFalse(TEXT("**the sale is refused**"), FMerchant::Sell(Scene.World, Cell, Gold, Message));
	TestEqual(TEXT("**with the walk sentence**"), Message, FString(FMerchant::WalkRefusal));
	TestNotNull(TEXT("the piece is still carried"), Scene.Bag()->ItemAt(Cell));
	TestEqual(TEXT("no gold was paid"), Scene.State->GetOwnGold(), static_cast<int64>(100));
	return true;
}

// ---------------------------------------------------------------------------
// M5. The screen: choose, ask, sell
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMerchantScreenTest,
	"Cataclysm.Merchant.TheScreenListsCarriedPiecesAndAsksBeforeItSells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two carried pieces. Sell with nothing chosen says to choose first. Choosing the first and pressing Sell asks, and
 * nothing is sold; choosing the second cancels the question; Sell twice on the second sells it and only it.
 */
bool FCataclysmMerchantScreenTest::RunTest(const FString&)
{
	using namespace CataclysmMerchantTest;
	FMerchantScene Scene = MakeScene(0);
	ON_SCOPE_EXIT { if (Scene.World) { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); } };
	if (!TestTrue(TEXT("set-up: the scene"), Scene.IsUsable()))
	{
		return false;
	}
	const int32 First = Scene.Bag()->AddItem(Piece(ECataclysmRarity::Everyday, 0));
	const int32 Second = Scene.Bag()->AddItem(Piece(ECataclysmRarity::Masterful, 0));
	UCataclysmMerchantWidget* Screen = NewObject<UCataclysmMerchantWidget>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: the screen"), Screen))
	{
		return false;
	}
	Screen->SetWorldContextForTests(Scene.World);
	Screen->Refresh();
	auto Shows = [Screen](const FString& Command)
	{
		return Screen->ButtonsShown().ContainsByPredicate([&Command](const TPair<FString, FString>& Each) { return Each.Key == Command; });
	};
	TestTrue(TEXT("**both pieces have a button**"), Shows(FString::Printf(TEXT("Cell:%d"), First)) && Shows(FString::Printf(TEXT("Cell:%d"), Second)));
	TestTrue(TEXT("and there is a Sell button"), Shows(TEXT("Sell")));

	TestFalse(TEXT("Sell with nothing chosen"), Screen->Press(TEXT("Sell")));
	TestEqual(TEXT("**says to choose first**"), Screen->MessageText(), FString(UCataclysmMerchantWidget::ChooseFirstText));

	TestTrue(TEXT("choose the first"), Screen->Press(FString::Printf(TEXT("Cell:%d"), First)));
	TestTrue(TEXT("Sell asks"), Screen->Press(TEXT("Sell")));
	TestTrue(TEXT("**it is asking**"), Screen->IsAsking());
	TestNotNull(TEXT("**and nothing is sold yet**"), Scene.Bag()->ItemAt(First));
	TestTrue(TEXT("**choose the second: the question is cancelled**"),
			 Screen->Press(FString::Printf(TEXT("Cell:%d"), Second)) && !Screen->IsAsking());
	Screen->Press(TEXT("Sell"));
	TestTrue(TEXT("**the second press sells**"), Screen->Press(TEXT("Sell")));
	TestNull(TEXT("**the second piece is gone**"), Scene.Bag()->ItemAt(Second));
	TestNotNull(TEXT("**the first is still carried**"), Scene.Bag()->ItemAt(First));
	TestEqual(TEXT("**5 gold held**"), Scene.State->GetOwnGold(), static_cast<int64>(5));
	TestEqual(TEXT("nothing is chosen after a sale"), Screen->ChosenCell(), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("the gold line"), Screen->GoldText(), FString(TEXT("Gold held: 5 gold")));
	return true;
}

// ---------------------------------------------------------------------------
// M6. The map's button: under the forge's, covering nothing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMerchantMapButtonPlaceTest,
	"Cataclysm.Merchant.TheMapButtonSitsUnderTheForgeButtonAndCoversNoCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The forge button's own test, one button lower, on the same four panels, against every city and every button. */
bool FCataclysmMerchantMapButtonPlaceTest::RunTest(const FString&)
{
	const FVector2D Forge = UCataclysmEmpireMapLayout::ForgeButtonPosition(1.0f, FVector2D(1600.0, 900.0));
	const FVector2D AtOne = UCataclysmEmpireMapLayout::MerchantButtonPosition(1.0f, FVector2D(1600.0, 900.0));
	const FVector2D SizeAtOne = UCataclysmEmpireMapLayout::MerchantButtonSize(1.0f);
	TestEqual(TEXT("in the forge button's column"), AtOne.X, Forge.X, 0.01);
	TestEqual(TEXT("**one button and one gap under it**"), AtOne.Y,
			  Forge.Y + UCataclysmEmpireMapLayout::ForgeButtonSize(1.0f).Y * 0.5 + UCataclysmEmpireMapLayout::CataclysmDungeonWalkGapPx
				  + SizeAtOne.Y * 0.5,
			  0.01);

	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->Build();
	const FVector2D Panels[] = {
		UCataclysmEmpireMapLayout::DiamondSize()
			+ FVector2D(2.0 * UCataclysmEmpireMapLayout::FitMarginPx, 2.0 * UCataclysmEmpireMapLayout::FitMarginPx),
		FVector2D(1600.0, 900.0),
		FVector2D(700.0, 1200.0),
		FVector2D(1852.0, 528.0),
	};
	auto Clear = [](const FVector2D& AtA, const FVector2D& SizeA, const FVector2D& AtB, const FVector2D& SizeB)
	{
		const FVector2D Gap = (AtA - AtB).GetAbs();
		return Gap.X >= (SizeA.X + SizeB.X) * 0.5 - 0.01 || Gap.Y >= (SizeA.Y + SizeB.Y) * 0.5 - 0.01;
	};
	int32 Checked = 0;
	for (const FVector2D& Panel : Panels)
	{
		const float Scale = UCataclysmEmpireMapLayout::ScaleToFit(Panel);
		const FVector2D Box = UCataclysmEmpireMapLayout::CitySize(Scale);
		const FVector2D Button = UCataclysmEmpireMapLayout::MerchantButtonSize(Scale);
		const FVector2D At = UCataclysmEmpireMapLayout::MerchantButtonPosition(Scale, Panel);
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the button is inside the panel"), Panel.X, Panel.Y),
				 At.X - Button.X * 0.5 >= -0.01 && At.Y - Button.Y * 0.5 >= -0.01
					 && At.X + Button.X * 0.5 <= Panel.X + 0.01 && At.Y + Button.Y * 0.5 <= Panel.Y + 0.01);
		for (const FCataclysmCity& City : Map->Cities)
		{
			TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the button covers no part of %s"), Panel.X, Panel.Y, *City.Name),
					 Clear(UCataclysmEmpireMapLayout::PositionFor(City.R, City.C, Scale, Panel), Box, At, Button));
			++Checked;
		}
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel nor the forge's button"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::ForgeButtonPosition(Scale, Panel),
					   UCataclysmEmpireMapLayout::ForgeButtonSize(Scale), At, Button));
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel nor the tree's button"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::EmpireTreeButtonPosition(Scale, Panel),
					   UCataclysmEmpireMapLayout::EmpireTreeButtonSize(Scale), At, Button));
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel nor the boss marker"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(Scale),
					   UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(Scale), At, Button));
	}
	TestEqual(TEXT("twenty-five cities on each of four panels"), Checked, 100);
	return true;
}

// ---------------------------------------------------------------------------
// M7. The map's button asks for the merchant's screen
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMerchantMapButtonPressTest,
	"Cataclysm.Merchant.TheMapButtonAsksForTheMerchantAndIsNoCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMerchantMapButtonPressTest::RunTest(const FString&)
{
	TestEqual(TEXT("the button's word"), UCataclysmEmpireMapLayout::MerchantButtonText(), FString(TEXT("Merchant")));
	UCataclysmEmpireMapWidget* Screen = NewObject<UCataclysmEmpireMapWidget>();
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	if (!TestNotNull(TEXT("set-up: the map screen"), Screen) || !TestNotNull(TEXT("set-up: a run"), Run))
	{
		return false;
	}
	Run->Begin(3);
	Screen->SetRunForTests(Run);
	const int32 Before = Screen->MerchantRequestCount();
	const int32 ForgeBefore = Screen->ForgeRequestCount();
	Screen->ClickMerchantForTests();
	TestEqual(TEXT("**the button asked once for the merchant's screen**"), Screen->MerchantRequestCount(), Before + 1);
	TestEqual(TEXT("**and not for the forge's**"), Screen->ForgeRequestCount(), ForgeBefore);
	Screen->ClickCityForTests(0);
	TestEqual(TEXT("CONTROL: a city's button does not ask for it"), Screen->MerchantRequestCount(), Before + 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
