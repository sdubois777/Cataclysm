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
#include "Interface/CataclysmForgeLayout.h"
#include "Interface/CataclysmForgeWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Cataclysmic Forge's screen and the empire map's "Forge" button: the owner's ruling of 2026-11-19 and the
 * coordinating session's rulings g and h. The screen is made with no Blueprint, as every screen test here is: the
 * automation command passes `-nullrhi`, so the lists and lines are read off the widget's own state.
 */
namespace CataclysmForgeScreenTest
{
	struct FScreenScene
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
	};

	FCataclysmItem PlainOf(const TCHAR* Base, int32 GearLevel, float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		Item.Residue = Residue;
		return Item;
	}

	/** A world with this project's game instance, a possessed player and a run; nothing carried and nothing worn. */
	FScreenScene MakeScene()
	{
		FScreenScene Out;
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
			// SOLO SELF-FOUND, WITH GOLD OF ITS OWN, so every craft can pay (#2830) and no account record is written.
			Partition.bSoloSelfFound = true;
			State->SetIdentity(TEXT("Smith"), Partition);
			State->SetOwnGold(1000000);
		}

		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2766, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;

		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}

	UCataclysmForgeWidget* MakeScreen(const FScreenScene& Scene)
	{
		UCataclysmForgeWidget* Screen = NewObject<UCataclysmForgeWidget>();
		if (Screen)
		{
			Screen->SetWorldContextForTests(Scene.World);
			Screen->RefreshDisplay();
		}
		return Screen;
	}
}

/**
 * The screen lists what is worn, in gear slot order, then what is carried, in cell order, each by the slot or the bag
 * and the name the player sees; the stones held, every level that has any; and a Combine for each of those levels
 * below +10. A material's cell is not a piece. Prints "LISTED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeScreenListTest,
	"Cataclysm.ForgeScreen.TheScreenListsWornThenCarriedPiecesAndTheStonesHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeScreenListTest::RunTest(const FString&)
{
	using namespace CataclysmForgeScreenTest;

	FScreenScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCataclysmItem First;
	FCataclysmItem Second;
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	Scene.Character->GetEquipment()->Equip(PlainOf(TEXT("Weapon_Greataxe"), 0, 40.0f), First, Second, Went);
	Scene.Character->GetEquipment()->Equip(PlainOf(TEXT("Head_Helm"), 2, 60.0f), First, Second, Went);
	UCataclysmInventoryComponent* Bag = Scene.Character->GetInventory();
	Bag->AddMaterial(UCataclysmForge::StoneFor(1), 16);
	Bag->AddItem(PlainOf(TEXT("Gloves_Handwraps"), 0, 50.0f));
	Bag->AddMaterial(UCataclysmForge::StoneFor(3), 5);

	UCataclysmForgeWidget* Screen = MakeScreen(Scene);
	if (!TestNotNull(TEXT("set-up: the screen"), Screen))
	{
		return false;
	}

	TArray<FString> Labels;
	for (const FCataclysmForgeEntry& Entry : Screen->Entries())
	{
		Labels.Add(Entry.Label);
	}
	AddInfo(FString::Printf(TEXT("LISTED: %s; %s"), *FString::Join(Labels, TEXT(" | ")), *Screen->StonesText()));

	const TArray<FString> Expected = {
		FString::Printf(TEXT("Worn, %s: Helm +2"), *UCataclysmGearSlots::DisplayName(ECataclysmGearSlot::Head)),
		FString::Printf(TEXT("Worn, %s: Greataxe +0"), *UCataclysmGearSlots::DisplayName(ECataclysmGearSlot::Weapon1)),
		FString(TEXT("Carried: Handwraps +0")),
	};
	TestEqual(TEXT("worn in slot order, then carried, by the names the player sees"), Labels, Expected);
	TestEqual(TEXT("the stones held"), Screen->StonesText(), FString(TEXT("Upgrade stones held: +1 x16, +3 x5")));
	TestEqual(TEXT("a combine for each level held"), Screen->CombineLevels(), TArray<int32>{1, 3});
	TestFalse(TEXT("nothing is chosen yet"), Screen->HasChosen());
	return true;
}

/**
 * An upgrade takes two presses: the first says what it costs and changes nothing, another press cancels it, and two
 * presses in a row raise the piece and say what it cost. During a walk the first press says the walk refusal and asks
 * for no second. Prints "TWO PRESSES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeScreenTwoPressesTest,
	"Cataclysm.ForgeScreen.AnUpgradeTakesTwoPressesAndAnyOtherPressCancels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeScreenTwoPressesTest::RunTest(const FString&)
{
	using namespace CataclysmForgeScreenTest;

	FScreenScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmInventoryComponent* Bag = Scene.Character->GetInventory();
	const int32 Gloves = Bag->AddItem(PlainOf(TEXT("Gloves_Handwraps"), 0, 50.0f));
	const int32 Boots = Bag->AddItem(PlainOf(TEXT("Boots_Treads"), 0, 50.0f));
	Bag->AddMaterial(UCataclysmForge::StoneFor(1), 4);
	const FName GlovesButton = UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Gloves));
	const FName BootsButton = UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Boots));

	UCataclysmForgeWidget* Screen = MakeScreen(Scene);
	if (!TestNotNull(TEXT("set-up: the screen"), Screen))
	{
		return false;
	}

	// THE FIRST PRESS ASKS AND CHANGES NOTHING.
	Screen->ClickPieceForTests(GlovesButton);
	TestTrue(TEXT("the gloves are chosen"), Screen->HasChosen());
	TestEqual(TEXT("the button says where to"), Screen->UpgradeText(), FString(TEXT("Upgrade to +1")));
	Screen->ClickUpgradeForTests();
	const FString Asked = Screen->MessageText();
	TestTrue(TEXT("the first press says what it costs"), Asked.StartsWith(TEXT("Press again to upgrade to +1")));
	TestEqual(TEXT("and changes nothing"), Bag->ItemAt(Gloves)->GearLevel, 0);

	// ANOTHER PRESS CANCELS IT: choosing the boots, then the gloves again, needs two presses again.
	Screen->ClickPieceForTests(BootsButton);
	Screen->ClickPieceForTests(GlovesButton);
	Screen->ClickUpgradeForTests();
	TestEqual(TEXT("after another press the next is a first press again"), Bag->ItemAt(Gloves)->GearLevel, 0);

	// TWO IN A ROW RAISE IT.
	const int32 DayBefore = Scene.Run->Day();
	Screen->ClickUpgradeForTests();
	const FString Done = Screen->MessageText();
	TestEqual(TEXT("two presses in a row raise the gloves"), Bag->ItemAt(Gloves)->GearLevel, 1);
	// THEN WHAT ITS DAY DID, when it did anything (#2857): this scene's board starts empty, and an empty board can bring
	// a surge. `Cataclysm.ForgeScreen.TheFirstPressNamesTheTimersItsDaysRunOutAndTheSecondWhatTheyDid` holds those words.
	const FString Account = UCataclysmGameInstance::CapitalDaysAccountIn(Scene.World);
	TestEqual(TEXT("and say what it cost"), Done,
			  FString(TEXT("Raised to +1. It cost 500 gold and 1 day of the run."))
				  + (Account.IsEmpty() ? FString() : TEXT(" ") + Account));
	TestEqual(TEXT("a day passed"), Scene.Run->Day(), DayBefore + 1);
	TestTrue(TEXT("the detail now reads +1"), Screen->DetailText().Contains(TEXT(", +1, residue 75.")));

	// DURING A WALK THE FIRST PRESS SAYS THE REFUSAL AND ASKS FOR NO SECOND.
	Scene.Run->Clock->EnterDungeon(/*DungeonId*/ 7);
	Screen->ClickPieceForTests(BootsButton);
	Screen->ClickUpgradeForTests();
	const FString InTheWalk = Screen->MessageText();
	Screen->ClickUpgradeForTests();
	TestEqual(TEXT("during a walk the press says the walk refusal"), InTheWalk,
			  FString(UCataclysmForgeCraft::WalkRefusal));
	TestEqual(TEXT("and a second press raises nothing"), Bag->ItemAt(Boots)->GearLevel, 0);
	AddInfo(FString::Printf(TEXT("TWO PRESSES: asked '%s'; done '%s'; in a walk '%s'"), *Asked, *Done, *InTheWalk));
	return true;
}

namespace CataclysmForgeScreenTest
{
	/**
	 * The run's opening wave fired, ONE ORDINARY DUNGEON on a standing city -- not a bonus dungeon, and not a Void
	 * dungeon, whose hit takes no defence -- set to run out after `Days` days, and every other timer pushed to 60. The
	 * dungeon, or INDEX_NONE when the wave stood none. Issue #2857.
	 */
	int32 ScreenDungeonDueIn(UCataclysmEmpireRun& Run, float Days)
	{
		Run.FireOpeningSurge();
		int32 Due = INDEX_NONE;
		for (const FCataclysmDungeon& Dungeon : Run.Dungeons)
		{
			const FCataclysmCity* City = Run.Map != nullptr ? Run.Map->Find(Dungeon.CityId) : nullptr;
			if (Dungeon.Type == ECataclysmDungeonType::Basic && !Dungeon.bBonus && !Dungeon.AddsVoidStacks()
				&& City != nullptr && !City->bFallen)
			{
				Due = Dungeon.DungeonId;
				break;
			}
		}
		if (Due != INDEX_NONE && Run.Clock != nullptr)
		{
			for (FCataclysmDungeonTimer& Timer : Run.Clock->Timers)
			{
				Timer.DaysUntilResolve = Timer.DungeonId == Due ? Days : 60.0f;
			}
		}
		return Due;
	}
}

/**
 * Issue #2857, the owner's ruling of 2026-11-22. Before an upgrade, the first press says which dungeons' timers run out
 * within the days it costs, the city each would hit and the day; an upgrade that ends before any timer asks the plain
 * question. The press that does it then says what its days did: the dungeon resolved and the city it hit. One ordinary
 * dungeon is due in three days and every other timer in 60; the gloves' upgrade costs one day and the boots', carrying
 * 250 residue, three. Prints "FORGE TIMERS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeScreenTimersTest,
	"Cataclysm.ForgeScreen.TheFirstPressNamesTheTimersItsDaysRunOutAndTheSecondWhatTheyDid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeScreenTimersTest::RunTest(const FString&)
{
	using namespace CataclysmForgeScreenTest;

	FScreenScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Due = ScreenDungeonDueIn(*Scene.Run, 3.0f);
	const FCataclysmDungeon* Dungeon = Due != INDEX_NONE ? Scene.Run->FindDungeon(Due) : nullptr;
	const FCataclysmCity* City = Dungeon != nullptr ? Scene.Run->Map->Find(Dungeon->CityId) : nullptr;
	if (!TestNotNull(TEXT("set-up: the opening wave stood an ordinary dungeon on a standing city"), City))
	{
		return false;
	}
	const FString DueName = Scene.Run->NameOfDungeon(Due);
	const FString CityName = City->Name;
	const int32 Today = Scene.Run->Day();

	UCataclysmInventoryComponent* Bag = Scene.Character->GetInventory();
	const int32 Gloves = Bag->AddItem(PlainOf(TEXT("Gloves_Handwraps"), 0, 0.0f));
	const int32 Boots = Bag->AddItem(PlainOf(TEXT("Boots_Treads"), 0, 250.0f));
	Bag->AddMaterial(UCataclysmForge::StoneFor(1), 4);
	UCataclysmForgeWidget* Screen = MakeScreen(Scene);
	if (!TestNotNull(TEXT("set-up: the screen"), Screen))
	{
		return false;
	}

	// AN UPGRADE THAT ENDS BEFORE ANY TIMER: one day, and the plain question.
	Screen->ClickPieceForTests(UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Gloves)));
	Screen->ClickUpgradeForTests();
	const FString Short = Screen->MessageText();
	const FCataclysmForgeUpgrade ShortPlan = UCataclysmForgeCraft::PlanFor(Scene.World, FCataclysmForgeTarget::Carried(Gloves));

	// AN UPGRADE WHOSE THREE DAYS RUN THE DUNGEON'S TIMER OUT: the question names it.
	Screen->ClickPieceForTests(UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Boots)));
	Screen->ClickUpgradeForTests();
	const FString Warned = Screen->MessageText();
	const int32 DayAfterAsking = Scene.Run->Day();

	// AND THE PRESS THAT DOES IT SAYS WHAT THE DAYS DID.
	Screen->ClickUpgradeForTests();
	const FString Done = Screen->MessageText();

	const FString Warning = FString::Printf(TEXT(" Within 3 days, 1 dungeon's timer runs out: dungeon %d (%s) hits %s on day %d."),
											Due, *DueName, *CityName, Today + 3);
	AddInfo(FString::Printf(TEXT("FORGE TIMERS: dungeon %d (%s) due in 3 on %s; one day: '%s'; three days: '%s'; done: '%s'"),
							Due, *DueName, *CityName, *Short, *Warned, *Done));

	TestEqual(TEXT("set-up: the gloves' upgrade costs one day"), ShortPlan.Days, 1);
	TestEqual(TEXT("an upgrade that ends before any timer asks the plain question, naming no dungeon"), Short,
			  UCataclysmForge::UpgradeQuestion(ShortPlan));
	TestTrue(TEXT("an upgrade whose days run a timer out asks the question"),
			 Warned.StartsWith(TEXT("Press again to upgrade to +1")));
	TestTrue(TEXT("and ends naming the dungeon, the city it hits and the day"), Warned.EndsWith(Warning));
	TestEqual(TEXT("asking passes no day"), DayAfterAsking, Today);
	TestEqual(TEXT("the second press raises the boots"), Bag->ItemAt(Boots)->GearLevel, 1);
	TestTrue(TEXT("and says what it cost"), Done.StartsWith(TEXT("Raised to +1. It cost ")));
	TestTrue(TEXT("then what the days did: the dungeon resolved, by its number and name, which only that part says"),
			 Done.Contains(FString::Printf(TEXT(" Meanwhile, days %d to %d: dungeons resolved: "), Today, Today + 3))
				 && Done.Contains(FString::Printf(TEXT("%d (%s)"), Due, *DueName)));
	TestTrue(TEXT("and the city it hit"), Done.Contains(FString::Printf(TEXT("%s lost "), *CityName)));
	TestNull(TEXT("CONTROL: the dungeon is gone from the run"), Scene.Run->FindDungeon(Due));
	return true;
}

/**
 * The map's "Forge" button is one city in size and sits under the empire upgrade tree's, against the right margin, as
 * the boss dungeon's Walk box sits under its marker: inside the panel, and covering no city, the marker, the Walk box
 * or the tree's button, on the panels the tree button's test uses. Prints "FORGE BUTTON".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeScreenMapButtonPlaceTest,
	"Cataclysm.ForgeScreen.TheMapButtonSitsUnderTheTreeButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeScreenMapButtonPlaceTest::RunTest(const FString&)
{
	// AT A SCALE OF ONE ON A 1600 BY 900 PANEL: one city, 172 by 48, its centre 24 + 86 in from the right and 24 + 48 + 4
	// + 24 down.
	const FVector2D AtOne = UCataclysmEmpireMapLayout::ForgeButtonPosition(1.0f, FVector2D(1600.0, 900.0));
	const FVector2D SizeAtOne = UCataclysmEmpireMapLayout::ForgeButtonSize(1.0f);
	TestEqual(TEXT("at a scale of one it is one city wide"), SizeAtOne.X, UCataclysmEmpireMapLayout::CitySize(1.0f).X,
			  0.01);
	TestEqual(TEXT("its centre is a margin and half its width from the right"), AtOne.X,
			  1600.0 - UCataclysmEmpireMapLayout::FitMarginPx - SizeAtOne.X * 0.5, 0.01);
	TestEqual(TEXT("and under the tree's button, a gap below it"), AtOne.Y,
			  UCataclysmEmpireMapLayout::FitMarginPx + UCataclysmEmpireMapLayout::EmpireTreeButtonSize(1.0f).Y
				  + UCataclysmEmpireMapLayout::CataclysmDungeonWalkGapPx + SizeAtOne.Y * 0.5,
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
		const FVector2D Button = UCataclysmEmpireMapLayout::ForgeButtonSize(Scale);
		const FVector2D At = UCataclysmEmpireMapLayout::ForgeButtonPosition(Scale, Panel);

		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the button is inside the panel"), Panel.X, Panel.Y),
				 At.X - Button.X * 0.5 >= -0.01 && At.Y - Button.Y * 0.5 >= -0.01
					 && At.X + Button.X * 0.5 <= Panel.X + 0.01 && At.Y + Button.Y * 0.5 <= Panel.Y + 0.01);

		for (const FCataclysmCity& City : Map->Cities)
		{
			TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the button covers no part of %s"), Panel.X, Panel.Y,
									 *City.Name),
					 Clear(UCataclysmEmpireMapLayout::PositionFor(City.R, City.C, Scale, Panel), Box, At, Button));
			++Checked;
		}

		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel it covers no part of the boss marker"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(Scale),
					   UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(Scale), At, Button));
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel nor of the Walk box"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::CataclysmDungeonWalkPosition(Scale),
					   UCataclysmEmpireMapLayout::CataclysmDungeonWalkSize(Scale), At, Button));
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel nor of the tree's button"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::EmpireTreeButtonPosition(Scale, Panel),
					   UCataclysmEmpireMapLayout::EmpireTreeButtonSize(Scale), At, Button));
	}
	AddInfo(FString::Printf(TEXT("FORGE BUTTON: %d city placements checked against the button"), Checked));
	TestEqual(TEXT("twenty-five cities on each of four panels"), Checked, 100);
	return true;
}

/**
 * A press of the map's "Forge" button asks for the forge's screen -- counted, because a headless test has no player
 * controller to open it -- and is no city's press and no tree's. Its words are "Forge". Prints "FORGE BUTTON PRESS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeScreenMapButtonPressTest,
	"Cataclysm.ForgeScreen.TheMapButtonAsksForTheForgeAndIsNoCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeScreenMapButtonPressTest::RunTest(const FString&)
{
	TestEqual(TEXT("the button's words"), UCataclysmEmpireMapLayout::ForgeButtonText(), FString(TEXT("Forge")));

	UCataclysmEmpireMapWidget* Screen = NewObject<UCataclysmEmpireMapWidget>();
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	if (!TestNotNull(TEXT("set-up: the map screen was created"), Screen)
		|| !TestNotNull(TEXT("set-up: and a run"), Run))
	{
		return false;
	}
	Run->Begin(3);
	Screen->SetRunForTests(Run);

	const int32 Before = Screen->ForgeRequestCount();
	Screen->ClickForgeForTests();
	const int32 AfterTheButton = Screen->ForgeRequestCount();
	const int32 TreeAfterTheButton = Screen->EmpireTreeRequestCount();
	const int32 CityAfterTheButton = Screen->LastClickedCityId();
	Screen->ClickCityForTests(0);
	const int32 AfterACity = Screen->ForgeRequestCount();

	AddInfo(FString::Printf(TEXT("FORGE BUTTON PRESS: requests %d, then %d after the button (tree %d, city %d), %d after "
								 "city 0"),
							Before, AfterTheButton, TreeAfterTheButton, CityAfterTheButton, AfterACity));
	TestEqual(TEXT("no request before the button is pressed"), Before, 0);
	TestEqual(TEXT("one when it is"), AfterTheButton, 1);
	TestEqual(TEXT("and no tree was asked for"), TreeAfterTheButton, 0);
	TestEqual(TEXT("and no city was clicked"), CityAfterTheButton, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("a city's press asks for no forge"), AfterACity, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
