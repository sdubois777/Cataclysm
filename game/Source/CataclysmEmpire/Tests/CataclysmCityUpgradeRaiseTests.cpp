// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"

/**
 * Raising a bought city upgrade to tier 2 and tier 3. Issue #1265, the coordinating session's rulings of 2026-11-22:
 * in place, in the same slot, for the next tier's gold and its build days (Q6); acting at its old tier while the raise
 * is built (Q7); the city's one build at a time (Q8); a spent one-time upgrade firing again at the new tier's full
 * figure (Q9); an interval's next trigger the sooner of the one set and a full new interval (Q10); a raised maximum
 * folded in as (1 + value) (Q11).
 *
 * BUILT BY HAND, as `CataclysmCityUpgradeTests.cpp` builds its upgrades, so no test depends on the DataTable. THE
 * PILLAR FOR THE TIMED TESTS, because no surge targets it while it is sealed; asserted at the end of each.
 */

namespace CataclysmCityUpgradeRaiseTest
{
	UCataclysmEmpireRun* MakeClearedRun()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* InSeed */ 1265, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	FCataclysmCityUpgrade Make(ECataclysmCityUpgradeEffect Effect, float Value, int32 Tier, float IntervalDays = 0.0f)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = FName(*FString::Printf(TEXT("Test_%s"), *UCataclysmCityUpgradeRules::EffectName(Effect)));
		Upgrade.Effect = Effect;
		Upgrade.Value = Value;
		Upgrade.Tier = Tier;
		Upgrade.IntervalDays = IntervalDays;
		Upgrade.bOneTimeUse = Effect == ECataclysmCityUpgradeEffect::RemoveDungeons
			|| Effect == ECataclysmCityUpgradeEffect::RestoreDefence
			|| Effect == ECataclysmCityUpgradeEffect::RestorePopulation;
		return Upgrade;
	}

	void PlaceDungeon(UCataclysmEmpireRun& Run, int32 CityId, int32 DungeonId, float ResolveDays)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.CityId = CityId;
		Dungeon.Floors = 10;
		Dungeon.ResolveDays = ResolveDays;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	const FCataclysmCityUpgrade* Held(const UCataclysmEmpireRun& Run, int32 CityId, FName RowName)
	{
		const FCataclysmCity* City = Run.Map->Find(CityId);
		return City != nullptr
			? City->Upgrades.FindByPredicate([RowName](const FCataclysmCityUpgrade& One) { return One.RowName == RowName; })
			: nullptr;
	}

	int32 FirstOrdinaryCity(const UCataclysmEmpireRun& Run)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.CityId != Run.Map->PillarId && !City.bFallen)
			{
				return City.CityId;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRaiseMaximumTest,
	"Cataclysm.CityUpgradeRaise.ARaisedMaximumActsAtItsOldTierUntilItsBuildIsFinishedAndKeepsAFullCityFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Increase max defense by 20%" bought on the Pillar, then raised to tier 2's 30% with 12 build days: for 11 days the
 * maximum stays the base times 1.2 and the upgrade at tier 1; on the twelfth it is the base times 1.3, the city still
 * full, the upgrade at tier 2, still in one slot, and its finished day still the day it was first finished. Prints
 * "RAISE MAXIMUM".
 */
bool FCataclysmCityUpgradeRaiseMaximumTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeRaiseTest;

	UCataclysmEmpireRun* Run = MakeClearedRun();
	const int32 PillarId = Run->Map->PillarId;
	const FCataclysmCity* Pillar = Run->Map->Find(PillarId);
	const float Base = Pillar->MaxDefence;
	const FCataclysmCityUpgrade TierOne = Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.2f, 1);

	const ECataclysmCityUpgradeResult Bought = Run->BuyCityUpgrade(PillarId, TierOne);
	const int32 FirstFinished = Held(*Run, PillarId, TierOne.RowName) ? Held(*Run, PillarId, TierOne.RowName)->BoughtDay : -1;
	const ECataclysmCityUpgradeResult Raised = Run->RaiseCityUpgrade(
		PillarId, Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.3f, 2), 0, 0, UCataclysmCityUpgradeRules::BuildDaysFor(2));
	Run->AdvanceDays(11);
	const float MaxWhileBuilding = Pillar->MaxDefence;
	const int32 TierWhileBuilding = Held(*Run, PillarId, TierOne.RowName) ? Held(*Run, PillarId, TierOne.RowName)->Tier : -1;
	Run->AdvanceDay();
	const FCataclysmCityUpgrade* After = Held(*Run, PillarId, TierOne.RowName);

	AddInfo(FString::Printf(TEXT("RAISE MAXIMUM: bought \"%s\", raised \"%s\"; base %.1f; after 11 days max %.1f at tier %d; "
								 "after 12 max %.1f, defence %.1f of %.1f, tier %d, finished day %d (first %d), %d held, "
								 "%d building; %d dungeons on the Pillar"),
							*UCataclysmCityUpgradeRules::ResultText(Bought), *UCataclysmCityUpgradeRules::ResultText(Raised),
							Base, MaxWhileBuilding, TierWhileBuilding, Pillar->MaxDefence, Run->Map->DefenceOf(*Pillar),
							Run->Map->EffectiveMaxDefence(*Pillar), After ? After->Tier : -1, After ? After->BoughtDay : -1,
							FirstFinished, Pillar->Upgrades.Num(), Pillar->Building.Num(), Run->DungeonsOn(PillarId).Num()));

	TestEqual(TEXT("set-up: bought"), Bought, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("the raise is accepted"), Raised, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("for 11 days the maximum is tier 1's"), MaxWhileBuilding, Base * 1.2f, 0.5f);
	TestEqual(TEXT("and the upgrade is at tier 1"), TierWhileBuilding, 1);
	if (!TestNotNull(TEXT("the upgrade is still held"), After))
	{
		return false;
	}
	TestEqual(TEXT("on the twelfth the maximum is tier 2's, the base times 1.3"), Pillar->MaxDefence, Base * 1.3f, 0.5f);
	TestEqual(TEXT("and a full city is still full"), Run->Map->DefenceOf(*Pillar), Run->Map->EffectiveMaxDefence(*Pillar),
			  0.5f);
	TestEqual(TEXT("the upgrade is at tier 2"), After->Tier, 2);
	TestEqual(TEXT("still in one slot"), Pillar->Upgrades.Num(), 1);
	TestTrue(TEXT("with nothing left building"), Pillar->Building.IsEmpty());
	TestEqual(TEXT("and its finished day kept, for Enduring Foundations"), After->BoughtDay, FirstFinished);
	TestEqual(TEXT("CONTROL: no dungeon came to the Pillar"), Run->DungeonsOn(PillarId).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRaiseOneTimeTest,
	"Cataclysm.CityUpgradeRaise.ASpentOneTimeUpgradeFiresAgainAtTheNewTiersFullFigure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Remove 25% of dungeons on this city" bought on a city holding four dungeons removes the soonest one; raised to
 * tier 2's 50%, it removes half of the three left, rounded, two, the soonest. "Restore city's defenses by 50%" on a
 * bitten city restores half its maximum; raised to tier 2's 75%, it restores again, to the maximum. Prints
 * "RAISE ONE-TIME".
 */
bool FCataclysmCityUpgradeRaiseOneTimeTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeRaiseTest;

	UCataclysmEmpireRun* Run = MakeClearedRun();
	const int32 CityId = FirstOrdinaryCity(*Run);
	if (!TestNotEqual(TEXT("set-up: an ordinary city"), CityId, static_cast<int32>(INDEX_NONE)))
	{
		return false;
	}
	PlaceDungeon(*Run, CityId, 941, 4.0f);
	PlaceDungeon(*Run, CityId, 942, 8.0f);
	PlaceDungeon(*Run, CityId, 943, 12.0f);
	PlaceDungeon(*Run, CityId, 944, 16.0f);

	Run->BuyCityUpgrade(CityId, Make(ECataclysmCityUpgradeEffect::RemoveDungeons, 0.25f, 1));
	const int32 AfterTierOne = Run->DungeonsOn(CityId).Num();
	const ECataclysmCityUpgradeResult Raised =
		Run->RaiseCityUpgrade(CityId, Make(ECataclysmCityUpgradeEffect::RemoveDungeons, 0.5f, 2));
	const TArray<int32> Left = Run->DungeonsOn(CityId);

	const FCataclysmCity* City = Run->Map->Find(CityId);
	Run->Map->Bite(CityId, 0.9f, 0.0f);
	const float Bitten = Run->Map->DefenceOf(*City);
	Run->BuyCityUpgrade(CityId, Make(ECataclysmCityUpgradeEffect::RestoreDefence, 0.5f, 1));
	const float AfterRestore = Run->Map->DefenceOf(*City);
	Run->RaiseCityUpgrade(CityId, Make(ECataclysmCityUpgradeEffect::RestoreDefence, 0.75f, 2));
	const float AfterRaise = Run->Map->DefenceOf(*City);
	const float Max = Run->Map->EffectiveMaxDefence(*City);

	AddInfo(FString::Printf(TEXT("RAISE ONE-TIME: four dungeons, %d after tier 1; raise \"%s\", %d left (%d); defence "
								 "%.1f bitten, %.1f restored, %.1f raised, of %.1f"),
							AfterTierOne, *UCataclysmCityUpgradeRules::ResultText(Raised), Left.Num(),
							Left.Num() > 0 ? Left[0] : -1, Bitten, AfterRestore, AfterRaise, Max));

	TestEqual(TEXT("tier 1's quarter of four removes one"), AfterTierOne, 3);
	TestEqual(TEXT("the raise is accepted"), Raised, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("tier 2's half of the three left, rounded, removes two"), Left.Num(), 1);
	TestTrue(TEXT("the latest one stays"), Left.Num() == 1 && Left[0] == 944);
	TestEqual(TEXT("tier 1's restore adds half the maximum"), AfterRestore, Bitten + Max * 0.5f, 0.5f);
	TestEqual(TEXT("the raise restores again, to the maximum"), AfterRaise, FMath::Min(Max, AfterRestore + Max * 0.75f),
			  0.5f);
	TestTrue(TEXT("CONTROL: the restore had room to act twice"), Bitten + Max * 0.5f < Max - 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRaiseIntervalTest,
	"Cataclysm.CityUpgradeRaise.ARaisedIntervalKeepsARepairThatWasDueAndShortensOneThatWasNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Every 20 days this city's defenses heal 5%" bought on day 0 first fires on day 20. Raised to tier 2's "every 10 days,
 * 10%" finished on day 15, its next trigger stays day 20, the sooner of 20 and 25. On a second city the same upgrade
 * raised on day 2 moves it to day 12, the sooner of 20 and 12. Prints "RAISE INTERVAL".
 */
bool FCataclysmCityUpgradeRaiseIntervalTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeRaiseTest;

	UCataclysmEmpireRun* Run = MakeClearedRun();
	const int32 PillarId = Run->Map->PillarId;
	const FCataclysmCityUpgrade Heal = Make(ECataclysmCityUpgradeEffect::HealDefenceEvery, 0.05f, 1, 20.0f);
	const FCataclysmCityUpgrade HealTwo = Make(ECataclysmCityUpgradeEffect::HealDefenceEvery, 0.1f, 2, 10.0f);

	UCataclysmEmpireRun* Early = MakeClearedRun();
	Run->BuyCityUpgrade(PillarId, Heal);
	Early->BuyCityUpgrade(PillarId, Heal);
	const int32 FirstTrigger = Held(*Run, PillarId, Heal.RowName) ? Held(*Run, PillarId, Heal.RowName)->NextTriggerDay : -1;

	Run->AdvanceDays(15);
	const ECataclysmCityUpgradeResult Late = Run->RaiseCityUpgrade(PillarId, HealTwo);
	Early->AdvanceDays(2);
	const ECataclysmCityUpgradeResult Soon = Early->RaiseCityUpgrade(PillarId, HealTwo);
	const FCataclysmCityUpgrade* LateHeld = Held(*Run, PillarId, Heal.RowName);
	const FCataclysmCityUpgrade* SoonHeld = Held(*Early, PillarId, Heal.RowName);

	AddInfo(FString::Printf(TEXT("RAISE INTERVAL: first trigger day %d; raised on day 15 \"%s\", next day %d, every %.0f "
								 "days %.2f; raised on day 2 \"%s\", next day %d"),
							FirstTrigger, *UCataclysmCityUpgradeRules::ResultText(Late),
							LateHeld ? LateHeld->NextTriggerDay : -1, LateHeld ? LateHeld->IntervalDays : -1.0f,
							LateHeld ? LateHeld->Value : -1.0f, *UCataclysmCityUpgradeRules::ResultText(Soon),
							SoonHeld ? SoonHeld->NextTriggerDay : -1));

	TestEqual(TEXT("set-up: tier 1 first fires on day 20"), FirstTrigger, 20);
	TestEqual(TEXT("the raise on day 15 is accepted"), Late, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("and the repair due on day 20 stays due on day 20"), LateHeld ? LateHeld->NextTriggerDay : -1, 20);
	TestEqual(TEXT("at tier 2's 10 days"), LateHeld ? LateHeld->IntervalDays : -1.0f, 10.0f);
	TestEqual(TEXT("and 10%"), LateHeld ? LateHeld->Value : -1.0f, 0.1f);
	TestEqual(TEXT("the raise on day 2 is accepted"), Soon, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("and brings the next repair to day 12"), SoonHeld ? SoonHeld->NextTriggerDay : -1, 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRaiseRefusalsTest,
	"Cataclysm.CityUpgradeRaise.EveryRefusalSaysWhichOneItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A raise is refused when the city does not hold the upgrade, when it is at tier 3 ("Already at tier 3."), for the
 * cleanse, for a tier other than the next, while another build is under way on the city, on a besieged or fallen city,
 * and for gold; and a raise does not take a slot: a full city may raise. Prints "RAISE REFUSALS".
 */
bool FCataclysmCityUpgradeRaiseRefusalsTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeRaiseTest;

	UCataclysmEmpireRun* Run = MakeClearedRun();
	const int32 PillarId = Run->Map->PillarId;
	const int32 CityId = FirstOrdinaryCity(*Run);
	const FCataclysmCityUpgrade Walls = Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.2f, 1);
	const FCataclysmCityUpgrade WallsTwo = Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.3f, 2);
	const FCataclysmCityUpgrade WallsThree = Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.4f, 3);

	const ECataclysmCityUpgradeResult NotHeld = Run->WouldRaiseCityUpgrade(PillarId, WallsTwo);
	FCataclysmCityUpgrade Cleanse = Make(ECataclysmCityUpgradeEffect::CleanseEveryCity, 0.0f, 2);
	const ECataclysmCityUpgradeResult NoLadder = Run->WouldRaiseCityUpgrade(PillarId, Cleanse);

	// A FULL CITY: three slots spent. A raise takes no slot, so it is not refused for the slots.
	Run->BuyCityUpgrade(PillarId, Walls);
	Run->BuyCityUpgrade(PillarId, Make(ECataclysmCityUpgradeEffect::ResistDefenceLoss, 0.25f, 1));
	Run->BuyCityUpgrade(PillarId, Make(ECataclysmCityUpgradeEffect::ResistPopulationLoss, 0.25f, 1));
	const int32 FreeSlots = Run->Map->FreeUpgradeSlots(PillarId);
	const ECataclysmCityUpgradeResult Skipping = Run->WouldRaiseCityUpgrade(PillarId, WallsThree);
	const ECataclysmCityUpgradeResult Short = Run->WouldRaiseCityUpgrade(PillarId, WallsTwo, 20000, 19999);
	const ECataclysmCityUpgradeResult Full = Run->WouldRaiseCityUpgrade(PillarId, WallsTwo, 20000, 20000);
	Run->RaiseCityUpgrade(PillarId, WallsTwo, 0, 0, UCataclysmCityUpgradeRules::BuildDaysFor(2));
	const ECataclysmCityUpgradeResult Building = Run->WouldRaiseCityUpgrade(
		PillarId, Make(ECataclysmCityUpgradeEffect::ResistDefenceLoss, 0.5f, 2));
	Run->AdvanceDays(UCataclysmCityUpgradeRules::BuildDaysFor(2));
	Run->RaiseCityUpgrade(PillarId, WallsThree);
	const ECataclysmCityUpgradeResult AtTop = Run->WouldRaiseCityUpgrade(PillarId, WallsThree);

	// A FRESH RUN FOR THE CITY'S OWN REFUSALS, so no surge of the days above has touched the city.
	UCataclysmEmpireRun* Other = MakeClearedRun();
	const ECataclysmCityUpgradeResult HeldThere = Other->BuyCityUpgrade(CityId, Walls);
	PlaceDungeon(*Other, CityId, 951, 30.0f);
	for (FCataclysmDungeon& Dungeon : Other->Dungeons)
	{
		if (Dungeon.DungeonId == 951)
		{
			Dungeon.SubType = ECataclysmDungeonSubType::Siege;
		}
	}
	const ECataclysmCityUpgradeResult Besieged = Other->WouldRaiseCityUpgrade(CityId, WallsTwo);
	Other->Map->Fall(CityId);
	const ECataclysmCityUpgradeResult Fallen = Other->WouldRaiseCityUpgrade(CityId, WallsTwo);

	const auto Say = [](ECataclysmCityUpgradeResult Result) { return UCataclysmCityUpgradeRules::ResultText(Result); };
	AddInfo(FString::Printf(TEXT("RAISE REFUSALS: not held \"%s\"; cleanse \"%s\"; full city, %d free, skipping a tier "
								 "\"%s\", short \"%s\", paid \"%s\"; building \"%s\"; at the top \"%s\"; besieged \"%s\"; "
								 "fallen \"%s\""),
							*Say(NotHeld), *Say(NoLadder), FreeSlots, *Say(Skipping), *Say(Short), *Say(Full),
							*Say(Building), *Say(AtTop), *Say(Besieged), *Say(Fallen)));

	TestEqual(TEXT("not held"), NotHeld, ECataclysmCityUpgradeResult::NotHeld);
	TestEqual(TEXT("the cleanse has no tier to raise to"), NoLadder, ECataclysmCityUpgradeResult::NoTierToRaiseTo);
	TestEqual(TEXT("set-up: the city is full"), FreeSlots, 0);
	TestEqual(TEXT("a tier other than the next is not one"), Skipping, ECataclysmCityUpgradeResult::NotAnUpgrade);
	TestEqual(TEXT("short of gold"), Short, ECataclysmCityUpgradeResult::NotEnoughGold);
	TestEqual(TEXT("a full city may raise, paid"), Full, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("one build at a time"), Building, ECataclysmCityUpgradeResult::BuildUnderWay);
	TestEqual(TEXT("at tier 3"), AtTop, ECataclysmCityUpgradeResult::AtTheTopTier);
	TestEqual(TEXT("saying so"), Say(ECataclysmCityUpgradeResult::AtTheTopTier), FString(TEXT("Already at tier 3.")));
	TestEqual(TEXT("set-up: the ordinary city holds the upgrade"), HeldThere, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("a besieged city"), Besieged, ECataclysmCityUpgradeResult::CityIsBesieged);
	TestEqual(TEXT("a fallen city"), Fallen, ECataclysmCityUpgradeResult::CityHasFallen);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
