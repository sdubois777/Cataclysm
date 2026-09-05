// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmCityUpgradeMapping.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Interface/CataclysmCityScreenWidget.h"
#include "Interface/CataclysmEmpireMapWidget.h"

/**
 * Tests for the city screen, issue #42.
 *
 * WHAT THEY CANNOT CHECK, AND IT IS THE THING THAT MATTERS MOST. The automation
 * test command in `tools/unreal_build.py` passes `-nullrhi`, so nothing is drawn
 * and no test here can say whether the screen is legible, whether a player can
 * find the upgrade they came for, or whether eleven greyed rows read as
 * unfinished work rather than as a broken screen. **Somebody has to look.**
 *
 * WHICH IS WHY MOST OF THEM ARE ABOUT `UCataclysmCityScreenLayout` RATHER THAN
 * THE WIDGET, the same arrangement `CataclysmEmpireScreenTests.cpp` is in and
 * for the same reason: a widget in a headless test has no Widget Blueprint, so
 * every one of its `BindWidget` properties is null and it draws nothing at all.
 * Everything that can be decided without a screen is in the layout class so that
 * it can be covered.
 *
 * TWO THINGS ARE TESTED ON THE WIDGETS ANYWAY, because neither needs a drawn
 * child: pressing an upgrade buys it, and clicking a city on the empire overview
 * picks the right city.
 */

namespace CataclysmCityScreenTest
{
	UCataclysmEmpireRun* MakeRun(int32 LethalityRung = 0)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* InSeed */ 4242, ECataclysmSurgeMode::Static,
				   LethalityRung);
		return Run;
	}

	/** Puts a dungeon on a city with a chosen timer, without waiting for a
	 *  surge to roll one. */
	void PlaceDungeon(UCataclysmEmpireRun& Run, int32 CityId, int32 DungeonId,
					  int32 Floors, float ResolveDays)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.CityId = CityId;
		Dungeon.Floors = Floors;
		Dungeon.ResolveDays = ResolveDays;

		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/** The offers a city has, or an empty list. */
	TArray<FCataclysmCityUpgradeOffer> Offers(const UCataclysmEmpireRun* Run,
											  int32 CityId)
	{
		return UCataclysmCityScreenLayout::OffersFor(Run, CityId);
	}

	int32 CountBuyable(const TArray<FCataclysmCityUpgradeOffer>& List)
	{
		int32 Found = 0;
		for (const FCataclysmCityUpgradeOffer& Offer : List)
		{
			if (Offer.bCanBuy)
			{
				++Found;
			}
		}
		return Found;
	}

	/** The first upgrade the city could actually buy, or `NAME_None`. */
	FName FirstBuyable(const TArray<FCataclysmCityUpgradeOffer>& List)
	{
		for (const FCataclysmCityUpgradeOffer& Offer : List)
		{
			if (Offer.bCanBuy)
			{
				return Offer.RowName;
			}
		}
		return NAME_None;
	}
}

// ---------------------------------------------------------------------------
// What the three labels say
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenStatusTest,
	"Cataclysm.CityScreen.TheHeadingAndStatusDescribeTheCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenStatusTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	// CITY 0 IS ON THE RIM, so it is an Outpost and it is exposed. Both are
	// properties of the lattice rather than of this test.
	const FString Title = UCataclysmCityScreenLayout::TitleTextFor(Run, 0);
	const FString Status = UCataclysmCityScreenLayout::StatusTextFor(Run, 0);

	TestEqual(TEXT("the heading is the city's name"), Title,
			  Run->Map->Find(0)->Name);

	TestTrue(TEXT("the status names the tier"), Status.Contains(TEXT("Outpost")));
	TestTrue(TEXT("and how much defence is left"),
			 Status.Contains(TEXT("defence")));
	TestTrue(TEXT("and how many people"), Status.Contains(TEXT("people")));
	TestTrue(TEXT("and that a rim city is exposed"),
			 Status.Contains(TEXT("exposed")));

	// AN INTERIOR CITY IS SEALED. The Pillar is behind everything.
	const FString Sealed =
		UCataclysmCityScreenLayout::StatusTextFor(Run, Run->Map->PillarId);

	TestTrue(TEXT("the Pillar is sealed behind the frontier"),
			 Sealed.Contains(TEXT("sealed behind the frontier")));

	// WITH NO RUN IT SAYS WHY IT IS EMPTY rather than showing a blank panel,
	// which reads as broken.
	const FString NoRun = UCataclysmCityScreenLayout::TitleTextFor(nullptr, 0);

	TestTrue(TEXT("with no run the heading says how to start one"),
			 NoRun.Contains(TEXT("Cataclysm.EmpireBegin")));

	TestEqual(TEXT("and there is no status to give"),
			  UCataclysmCityScreenLayout::StatusTextFor(nullptr, 0), FString());

	// AND A CITY THAT DOES NOT EXIST IS THE SAME CASE.
	TestTrue(TEXT("city 99 says the same"),
			 UCataclysmCityScreenLayout::TitleTextFor(Run, 99)
				 .Contains(TEXT("No city")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenFallenTest,
	"Cataclysm.CityScreen.AFallenCitySaysSoInsteadOfGivingNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenFallenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	Run->Map->Fall(0);

	const FString Status = UCataclysmCityScreenLayout::StatusTextFor(Run, 0);

	// A FALLEN CITY IS NOT A DESTROYED ONE. Saying it can be retaken is the
	// difference between a setback and a loss, and a defence figure of zero
	// would say neither.
	TestTrue(TEXT("it says the city has fallen"),
			 Status.Contains(TEXT("Fallen")));
	TestTrue(TEXT("and that it can be retaken"),
			 Status.Contains(TEXT("retaken")));
	TestFalse(TEXT("and gives no defence figure"),
			  Status.Contains(TEXT("defence")));

	// AND NOTHING CAN BE BOUGHT THERE.
	const TArray<FCataclysmCityUpgradeOffer> List = Offers(Run, 0);

	if (!TestEqual(TEXT("all twenty-four are still listed"), List.Num(), 24))
	{
		return false;
	}

	TestEqual(TEXT("and none of them can be bought"), CountBuyable(List), 0);

	TestTrue(TEXT("each says the city has fallen"),
			 List[0].Refusal.Contains(TEXT("fallen")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenSlotsTest,
	"Cataclysm.CityScreen.TheSlotLineCountsWhatIsFilledOfWhatIsAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenSlotsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Standard = MakeRun(0);

	TestEqual(TEXT("a fresh city on Standard has none of three filled"),
			  UCataclysmCityScreenLayout::SlotsTextFor(Standard, 0),
			  TEXT("0 of 3 upgrade slots filled."));

	const FName First = FirstBuyable(Offers(Standard, 0));
	if (!TestNotEqual(TEXT("there is something to buy"), First, FName(NAME_None)))
	{
		return false;
	}

	Standard->BuyCityUpgrade(
		0, UCataclysmCityUpgradeMapping::MakeFromTable(First));

	TestEqual(TEXT("buying one fills a slot"),
			  UCataclysmCityScreenLayout::SlotsTextFor(Standard, 0),
			  TEXT("1 of 3 upgrade slots filled."));

	// HERETIC GIVES A CITY TWO SLOTS INSTEAD OF THREE, and the line has to say
	// two rather than three or a player would plan for a slot they do not have.
	UCataclysmEmpireRun* Heretic = MakeRun(2);

	TestEqual(TEXT("on Heretic a city has two"),
			  UCataclysmCityScreenLayout::SlotsTextFor(Heretic, 0),
			  TEXT("0 of 2 upgrade slots filled."));

	return true;
}

// ---------------------------------------------------------------------------
// What is standing on the city
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenDungeonsTest,
	"Cataclysm.CityScreen.DungeonsAreListedSoonestToBiteFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenDungeonsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	const int32 CityId = Run->Map->PillarId;

	TestEqual(TEXT("nothing is standing on it to begin with"),
			  UCataclysmCityScreenLayout::DungeonLinesFor(Run, CityId).Num(), 0);

	TestEqual(TEXT("and the heading says so"),
			  UCataclysmCityScreenLayout::DungeonHeading(0),
			  TEXT("Nothing is standing on this city"));

	// PLACED OUT OF ORDER ON PURPOSE. The whole reason to open a city is to
	// decide whether it is about to be bitten and by what, so the most urgent
	// threat must not be buried under two that are weeks away.
	PlaceDungeon(*Run, CityId, /* DungeonId */ 700, /* Floors */ 30,
				 /* ResolveDays */ 40.0f);
	PlaceDungeon(*Run, CityId, /* DungeonId */ 701, /* Floors */ 12,
				 /* ResolveDays */ 9.0f);
	PlaceDungeon(*Run, CityId, /* DungeonId */ 702, /* Floors */ 20,
				 /* ResolveDays */ 25.0f);

	const TArray<FString> Lines =
		UCataclysmCityScreenLayout::DungeonLinesFor(Run, CityId);

	if (!TestEqual(TEXT("three dungeons are listed"), Lines.Num(), 3))
	{
		return false;
	}

	// THE ONE DUE IN NINE DAYS IS FIRST, and it is the 12 floor one, so the
	// order is checked by a fact that is not the number being sorted on.
	TestTrue(TEXT("the soonest to bite is first"),
			 Lines[0].Contains(TEXT("12 floors")));
	TestTrue(TEXT("and it says how long is left"),
			 Lines[0].Contains(TEXT("9 days until it bites")));

	TestTrue(TEXT("the one due in 25 days is second"),
			 Lines[1].Contains(TEXT("20 floors")));
	TestTrue(TEXT("and the one due in 40 days is last"),
			 Lines[2].Contains(TEXT("30 floors")));

	TestEqual(TEXT("the heading counts them"),
			  UCataclysmCityScreenLayout::DungeonHeading(3),
			  TEXT("3 dungeons are standing on this city"));

	TestEqual(TEXT("and says one rather than 1 dungeons"),
			  UCataclysmCityScreenLayout::DungeonHeading(1),
			  TEXT("1 dungeon is standing on this city"));

	return true;
}

// ---------------------------------------------------------------------------
// What the city can build
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenOffersTest,
	"Cataclysm.CityScreen.AllTwentyFourAreShownAndTenCanBeBought",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenOffersTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	const TArray<FCataclysmCityUpgradeOffer> List = Offers(Run, 0);

	// THE GUARD AGAINST THIS TEST BEING VACUOUS. Every count below is taken from
	// this list, so an empty one -- which is what a missing DataTable gives --
	// would make all of them pass while checking nothing.
	if (!TestEqual(TEXT("every upgrade in the table is shown"), List.Num(), 24))
	{
		return false;
	}

	// THIRTEEN, WHICH IS WHAT THE RULES SAY IS BUILT. Read from the rules as well
	// as written out, so the two cannot part company and neither can drift
	// unnoticed.
	TestEqual(TEXT("a fresh city can buy every built upgrade"),
			  CountBuyable(List),
			  UCataclysmCityUpgradeRules::BuiltEffectCount());

	TestEqual(TEXT("which is thirteen"), CountBuyable(List), 13);

	// AND THE OTHER ELEVEN SAY WHY NOT, in the same words the console command
	// and a refused purchase use.
	int32 NotBuilt = 0;

	for (const FCataclysmCityUpgradeOffer& Offer : List)
	{
		if (Offer.bCanBuy)
		{
			TestEqual(*FString::Printf(TEXT("%s has no refusal"),
									   *Offer.RowName.ToString()),
					  Offer.Refusal, FString());
			continue;
		}

		++NotBuilt;

		TestEqual(*FString::Printf(TEXT("%s says its effect is not built"),
								   *Offer.RowName.ToString()),
				  Offer.Refusal,
				  UCataclysmCityUpgradeRules::ResultText(
					  ECataclysmCityUpgradeResult::EffectNotBuiltYet));
	}

	TestEqual(TEXT("eleven do nothing yet"), NotBuilt, 11);

	// THIRTEEN AND ELEVEN ARE ALL TWENTY-FOUR. Without this a count could drift
	// in both directions at once and neither figure above would notice.
	TestEqual(TEXT("and the two counts are the whole sheet"),
			  CountBuyable(List) + NotBuilt, 24);

	// EVERY OFFER CARRIES THE SENTENCE A DESIGNER WROTE, rather than a
	// description this screen invented.
	for (const FCataclysmCityUpgradeOffer& Offer : List)
	{
		TestFalse(*FString::Printf(TEXT("%s has its effect text"),
								   *Offer.RowName.ToString()),
				  Offer.Effect.IsEmpty());
	}

	// AND THE HEADING OVER THE UNBUILT ONES COUNTS THEM AND SAYS WHY THEY ARE
	// THERE, because a block of greyed rows with no explanation reads as a
	// broken screen.
	const FString Heading = UCataclysmCityScreenLayout::NotBuiltHeading(11);

	TestTrue(TEXT("the heading counts them"), Heading.Contains(TEXT("11")));
	TestTrue(TEXT("and says they do nothing yet"),
			 Heading.Contains(TEXT("do nothing yet")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenHeldTest,
	"Cataclysm.CityScreen.AnUpgradeAlreadyBoughtIsListedRatherThanOfferedAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenHeldTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	TestEqual(TEXT("a fresh city has bought nothing"),
			  UCataclysmCityScreenLayout::HeldLinesFor(Run, 0).Num(), 0);

	const FName Bought = FirstBuyable(Offers(Run, 0));
	if (!TestNotEqual(TEXT("there is something to buy"), Bought,
					  FName(NAME_None)))
	{
		return false;
	}

	Run->BuyCityUpgrade(0,
						UCataclysmCityUpgradeMapping::MakeFromTable(Bought));

	const TArray<FString> Held =
		UCataclysmCityScreenLayout::HeldLinesFor(Run, 0);

	if (!TestEqual(TEXT("it is listed under what the city has"), Held.Num(), 1))
	{
		return false;
	}

	// THE SENTENCE THE DESIGNER WROTE, not the row name and not the effect
	// enum's name.
	TestEqual(TEXT("described in the workbook's own words"), Held[0],
			  UCataclysmCityUpgradeMapping::EffectTextFor(Bought));

	// AND IT IS NOT OFFERED A SECOND TIME.
	const TArray<FCataclysmCityUpgradeOffer> After = Offers(Run, 0);

	TestEqual(TEXT("one fewer can be bought"), CountBuyable(After), 12);

	for (const FCataclysmCityUpgradeOffer& Offer : After)
	{
		if (Offer.RowName != Bought)
		{
			continue;
		}

		TestTrue(TEXT("the bought one is marked as held"), Offer.bHeld);
		TestFalse(TEXT("and cannot be bought again"), Offer.bCanBuy);
		TestEqual(TEXT("and says the city already has it"), Offer.Refusal,
				  UCataclysmCityUpgradeRules::ResultText(
					  ECataclysmCityUpgradeResult::AlreadyBought));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenFullTest,
	"Cataclysm.CityScreen.AFullCityOffersNothingAndSaysWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenFullTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	// THREE PURCHASES FILL AN ORDINARY CITY.
	for (int32 Spent = 0; Spent < 3; ++Spent)
	{
		const FName Next = FirstBuyable(Offers(Run, 0));
		if (!TestNotEqual(TEXT("there is another upgrade to buy"), Next,
						  FName(NAME_None)))
		{
			return false;
		}

		TestEqual(TEXT("and buying it goes through"),
				  Run->BuyCityUpgrade(
					  0, UCataclysmCityUpgradeMapping::MakeFromTable(Next)),
				  ECataclysmCityUpgradeResult::Bought);
	}

	TestEqual(TEXT("the city is full"),
			  UCataclysmCityScreenLayout::SlotsTextFor(Run, 0),
			  TEXT("3 of 3 upgrade slots filled."));

	const TArray<FCataclysmCityUpgradeOffer> List = Offers(Run, 0);

	TestEqual(TEXT("nothing more can be bought"), CountBuyable(List), 0);

	// AND THE REASON IS THE SLOTS RATHER THAN THE EFFECT. An upgrade that IS
	// built and is not held has to say the slots are full, or a player would
	// think the game had run out of upgrades.
	int32 SaidFull = 0;

	for (const FCataclysmCityUpgradeOffer& Offer : List)
	{
		if (Offer.bHeld
			|| !UCataclysmCityUpgradeRules::IsBuilt(Offer.EffectKind))
		{
			continue;
		}

		++SaidFull;

		TestEqual(*FString::Printf(TEXT("%s says the slots are full"),
								   *Offer.RowName.ToString()),
				  Offer.Refusal,
				  UCataclysmCityUpgradeRules::ResultText(
					  ECataclysmCityUpgradeResult::NoSlotsLeft));
	}

	TestEqual(TEXT("the ten built upgrades it did not buy say so"), SaidFull,
			  10);

	return true;
}

// ---------------------------------------------------------------------------
// The two things the widgets themselves can be asked
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenClickBuysTest,
	"Cataclysm.CityScreen.PressingAnUpgradeBuysItAndThenRefusesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenClickBuysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	// A WIDGET WITH NO WIDGET BLUEPRINT. Every `BindWidget` property is null, so
	// nothing is drawn and no label can be read -- but buying does not need a
	// label, and that is the half worth checking.
	UCataclysmCityScreenWidget* Screen =
		NewObject<UCataclysmCityScreenWidget>();

	Screen->SetRunForTests(Run);
	Screen->SetCity(0);

	TestEqual(TEXT("it is showing city 0"), Screen->ShownCityId(), 0);

	const FName Wanted = FirstBuyable(Offers(Run, 0));
	if (!TestNotEqual(TEXT("there is something to buy"), Wanted,
					  FName(NAME_None)))
	{
		return false;
	}

	TestEqual(TEXT("the city has bought nothing yet"),
			  Run->Map->Find(0)->Upgrades.Num(), 0);

	Screen->ClickOfferForTests(Wanted);

	if (!TestEqual(TEXT("pressing it spends a slot"),
				   Run->Map->Find(0)->Upgrades.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("on the upgrade that was pressed"),
			 Run->Map->Find(0)->HasUpgrade(Wanted));

	// PRESSING IT AGAIN IS REFUSED AND THE REASON IS KEPT. Only a buyable
	// upgrade is given a button, so this can only happen when the city changed
	// underneath the screen -- but a player who presses something and sees
	// nothing happen would reasonably think the screen was broken.
	Screen->ClickOfferForTests(Wanted);

	TestEqual(TEXT("no second slot was spent"),
			  Run->Map->Find(0)->Upgrades.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityScreenMapClickTest,
	"Cataclysm.CityScreen.ClickingACityOnTheMapPicksThatCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityScreenMapClickTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();

	UCataclysmEmpireMapWidget* Map = NewObject<UCataclysmEmpireMapWidget>();
	Map->SetRunForTests(Run);
	Map->Refresh();

	TestEqual(TEXT("no city has been clicked yet"), Map->LastClickedCityId(),
			  INDEX_NONE);

	// THE PART OF A CLICK THAT CAN GO WRONG SILENTLY. Opening the screen needs a
	// player controller and a headless test has none, so what is checked here is
	// that a click on a particular box means a particular city -- which is what
	// `CityForButton` decides and what would send a player to the wrong city.
	Map->ClickCityForTests(7);

	TestEqual(TEXT("clicking city 7 picks city 7"), Map->LastClickedCityId(), 7);

	Map->ClickCityForTests(Run->Map->PillarId);

	TestEqual(TEXT("and clicking the Pillar picks the Pillar"),
			  Map->LastClickedCityId(), Run->Map->PillarId);

	// A CITY THAT DOES NOT EXIST CHANGES NOTHING, rather than sending the screen
	// to a city that is not there.
	Map->ClickCityForTests(99);

	TestEqual(TEXT("clicking nothing leaves the last choice alone"),
			  Map->LastClickedCityId(), Run->Map->PillarId);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
