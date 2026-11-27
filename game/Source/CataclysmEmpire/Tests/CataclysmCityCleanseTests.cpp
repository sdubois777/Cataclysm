// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Tests/CataclysmTestRun.h"

/**
 * The cleanse, the last city upgrade: "Cleanse every player city of half of the dungeons on them excluding Quest and
 * Fallen City dungeons. The cities lose 50% of their remaining defenses and population. Can only purchase once, and
 * will only be available on T3 and above." Issue #2838, built as the coordinating session ruled on 2026-11-22:
 * difficulty tier 3 and above, once a run on any city, soonest-to-resolve first with `RemoveDungeons`' rounding,
 * every city that has not fallen, and the Cataclysm's own dungeon neither counted nor removed (derived: V13).
 *
 * BUILT BY HAND, as `CataclysmCityUpgradeTests.cpp` builds its upgrades, so no test depends on the DataTable.
 */

namespace CataclysmCityCleanseTest
{
	/** A run at difficulty tier 3, the lowest the cleanse is offered at, with nothing standing on the map. */
	UCataclysmEmpireRun* MakeClearedRun(int32 DifficultyTier = 3)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(/* InSeed */ 2838, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, DifficultyTier);
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	FCataclysmCityUpgrade Cleanse()
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = FName(TEXT("Test_Cleanse"));
		Upgrade.Effect = ECataclysmCityUpgradeEffect::CleanseEveryCity;
		Upgrade.bOneTimeUse = true;
		return Upgrade;
	}

	/** A dungeon of a kind on a city with a chosen timer, written to the run and the clock together. */
	void PlaceDungeon(UCataclysmEmpireRun& Run, int32 CityId, int32 DungeonId, ECataclysmDungeonType Type,
					  float ResolveDays)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.CityId = CityId;
		Dungeon.Type = Type;
		Dungeon.Floors = 10;
		Dungeon.ResolveDays = ResolveDays;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	bool Stands(const UCataclysmEmpireRun& Run, int32 DungeonId)
	{
		return Run.Dungeons.ContainsByPredicate(
			[DungeonId](const FCataclysmDungeon& Dungeon) { return Dungeon.DungeonId == DungeonId; });
	}

	/** The first cities that are neither the Pillar nor fallen, in map order. */
	TArray<int32> OrdinaryCities(const UCataclysmEmpireRun& Run, int32 Wanted)
	{
		TArray<int32> Found;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.CityId != Run.Map->PillarId && !City.bFallen && Found.Num() < Wanted)
			{
				Found.Add(City.CityId);
			}
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityCleanseHalvesTest,
	"Cataclysm.CityCleanse.EachCityLosesHalfItsCountedDungeonsSoonestFirstAndHalfWhatItHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Bought at difficulty tier 3 with no build days, so it is finished at once:
 *   - city A holds three ordinary dungeons resolving in 5, 10 and 15 days, and a Quest dungeon (1 day), the
 *     Cataclysm's own dungeon (2 days) and a Fallen City dungeon (3 days), each sooner than any ordinary one, so a
 *     cleanse that counted any of them would take it first. Half of the three ordinary ones, rounded, is two: the 5
 *     and 10 day ones go, and the other four stay;
 *   - city B holds one ordinary dungeon, and half of one, rounded, is one: it goes;
 *   - city C has fallen, and its ordinary dungeon stays;
 *   - A and B each keep half of the defence and population they held; C is untouched.
 * Prints "CLEANSE".
 */
bool FCataclysmCityCleanseHalvesTest::RunTest(const FString&)
{
	using namespace CataclysmCityCleanseTest;

	UCataclysmEmpireRun* Run = MakeClearedRun();
	const TArray<int32> Cities = OrdinaryCities(*Run, 3);
	if (!TestEqual(TEXT("set-up: three ordinary cities"), Cities.Num(), 3))
	{
		return false;
	}
	const int32 A = Cities[0];
	const int32 B = Cities[1];
	const int32 C = Cities[2];

	PlaceDungeon(*Run, A, 901, ECataclysmDungeonType::Basic, 5.0f);
	PlaceDungeon(*Run, A, 902, ECataclysmDungeonType::Basic, 10.0f);
	PlaceDungeon(*Run, A, 903, ECataclysmDungeonType::Basic, 15.0f);
	PlaceDungeon(*Run, A, 904, ECataclysmDungeonType::Quest, 1.0f);
	PlaceDungeon(*Run, A, 905, ECataclysmDungeonType::Cataclysm, 2.0f);
	PlaceDungeon(*Run, A, 906, ECataclysmDungeonType::FallenCity, 3.0f);
	PlaceDungeon(*Run, B, 911, ECataclysmDungeonType::Basic, 8.0f);
	PlaceDungeon(*Run, C, 921, ECataclysmDungeonType::Basic, 4.0f);

	// A CITY SHORT OF ITS MAXIMUM, so "half of what it holds" and "half of its maximum" give different figures.
	Run->Map->Bite(A, 0.2f, 0.1f);
	Run->Map->Fall(C);
	const FCataclysmCity* CityA = Run->Map->Find(A);
	const FCataclysmCity* CityB = Run->Map->Find(B);
	const FCataclysmCity* CityC = Run->Map->Find(C);
	const float DefenceA = Run->Map->DefenceOf(*CityA);
	const float PopulationA = Run->Map->PopulationOf(*CityA);
	const float DefenceB = Run->Map->DefenceOf(*CityB);
	const float PopulationB = Run->Map->PopulationOf(*CityB);
	const float DefenceC = CityC->Defence;
	const float PopulationC = CityC->Population;
	if (!TestTrue(TEXT("set-up: city A holds less than its maximum"), DefenceA < CityA->MaxDefence - 1.0f)
		|| !TestTrue(TEXT("set-up: city C has fallen"), CityC->bFallen))
	{
		return false;
	}

	const ECataclysmCityUpgradeResult Result = Run->BuyCityUpgrade(A, Cleanse());

	AddInfo(FString::Printf(TEXT("CLEANSE: %s; A's dungeons 901 %d 902 %d 903 %d quest %d cataclysm %d fallen %d; B's "
								 "%d; C's %d; A defence %.1f to %.1f, population %.1f to %.1f; B defence %.1f to %.1f; "
								 "C defence %.1f to %.1f, population %.1f to %.1f; flag %d"),
							*UCataclysmCityUpgradeRules::ResultText(Result), Stands(*Run, 901), Stands(*Run, 902),
							Stands(*Run, 903), Stands(*Run, 904), Stands(*Run, 905), Stands(*Run, 906),
							Stands(*Run, 911), Stands(*Run, 921), DefenceA, CityA->Defence, PopulationA,
							CityA->Population, DefenceB, CityB->Defence, DefenceC, CityC->Defence, PopulationC,
							CityC->Population, Run->bCleanseBoughtThisRun ? 1 : 0));

	TestEqual(TEXT("it is bought at difficulty tier 3"), Result, ECataclysmCityUpgradeResult::Bought);
	TestTrue(TEXT("and spent for the run"), Run->bCleanseBoughtThisRun);
	TestFalse(TEXT("city A's soonest ordinary dungeon is removed"), Stands(*Run, 901));
	TestFalse(TEXT("and its next"), Stands(*Run, 902));
	TestTrue(TEXT("and its third stays: half of three, rounded, is two"), Stands(*Run, 903));
	TestTrue(TEXT("its Quest dungeon stays, though it resolves soonest"), Stands(*Run, 904));
	TestTrue(TEXT("the Cataclysm's own dungeon stays (derived: V13)"), Stands(*Run, 905));
	TestTrue(TEXT("and its Fallen City dungeon stays"), Stands(*Run, 906));
	TestFalse(TEXT("city B's one dungeon is removed: half of one, rounded, is one"), Stands(*Run, 911));
	TestTrue(TEXT("the fallen city C keeps its dungeon"), Stands(*Run, 921));
	TestEqual(TEXT("city A keeps half the defence it held"), CityA->Defence, DefenceA * 0.5f, 0.01f);
	TestEqual(TEXT("and half the population"), CityA->Population, PopulationA * 0.5f, 0.01f);
	TestEqual(TEXT("city B keeps half its defence"), CityB->Defence, DefenceB * 0.5f, 0.01f);
	TestEqual(TEXT("and half its population"), CityB->Population, PopulationB * 0.5f, 0.01f);
	TestEqual(TEXT("the fallen city C loses no defence"), CityC->Defence, DefenceC, 0.01f);
	TestEqual(TEXT("and no population"), CityC->Population, PopulationC, 0.01f);
	TestTrue(TEXT("CONTROL: half of what A held is not half of its maximum"),
			 FMath::Abs(DefenceA * 0.5f - CityA->MaxDefence * 0.5f) > 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityCleanseGatesTest,
	"Cataclysm.CityCleanse.ItIsOfferedFromDifficultyTierThreeAndOnceARun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * At difficulty tiers 1 and 2 the cleanse is refused with "Available from difficulty tier 3."; at tier 3 it is
 * offered. Bought on one city with its 6 build days, it is refused on every other city at once, while it is still
 * being built, with "Already bought this run."; another upgrade is not. A new run may buy it again. Prints
 * "CLEANSE GATES".
 */
bool FCataclysmCityCleanseGatesTest::RunTest(const FString&)
{
	using namespace CataclysmCityCleanseTest;

	UCataclysmEmpireRun* TierOne = MakeClearedRun(1);
	UCataclysmEmpireRun* TierTwo = MakeClearedRun(2);
	UCataclysmEmpireRun* Run = MakeClearedRun(3);
	const TArray<int32> Cities = OrdinaryCities(*Run, 2);
	if (!TestEqual(TEXT("set-up: two ordinary cities"), Cities.Num(), 2))
	{
		return false;
	}

	const ECataclysmCityUpgradeResult AtOne = TierOne->WouldBuyCityUpgrade(Cities[0], Cleanse(), 0, 0);
	const ECataclysmCityUpgradeResult AtTwo = TierTwo->WouldBuyCityUpgrade(Cities[0], Cleanse(), 0, 0);
	const ECataclysmCityUpgradeResult AtThree = Run->WouldBuyCityUpgrade(Cities[0], Cleanse(), 0, 0);
	const ECataclysmCityUpgradeResult Bought =
		Run->BuyCityUpgrade(Cities[0], Cleanse(), 0, 0, UCataclysmCityUpgradeRules::BuildDaysFor(1));
	const bool bBuilding = Run->BuildOn(Cities[0]) != nullptr;
	const ECataclysmCityUpgradeResult Again = Run->WouldBuyCityUpgrade(Cities[1], Cleanse(), 0, 0);

	FCataclysmCityUpgrade Walls;
	Walls.RowName = FName(TEXT("Test_Walls"));
	Walls.Effect = ECataclysmCityUpgradeEffect::MaxDefence;
	Walls.Value = 0.2f;
	const ECataclysmCityUpgradeResult Other = Run->WouldBuyCityUpgrade(Cities[1], Walls, 0, 0);

	UCataclysmEmpireRun* NextRun = MakeClearedRun(3);
	const ECataclysmCityUpgradeResult InTheNextRun = NextRun->WouldBuyCityUpgrade(Cities[1], Cleanse(), 0, 0);

	AddInfo(FString::Printf(TEXT("CLEANSE GATES: tier 1 \"%s\"; tier 2 \"%s\"; tier 3 \"%s\"; bought \"%s\", building "
								 "%d; another city \"%s\"; another upgrade \"%s\"; the next run \"%s\""),
							*UCataclysmCityUpgradeRules::ResultText(AtOne), *UCataclysmCityUpgradeRules::ResultText(AtTwo),
							*UCataclysmCityUpgradeRules::ResultText(AtThree),
							*UCataclysmCityUpgradeRules::ResultText(Bought), bBuilding ? 1 : 0,
							*UCataclysmCityUpgradeRules::ResultText(Again), *UCataclysmCityUpgradeRules::ResultText(Other),
							*UCataclysmCityUpgradeRules::ResultText(InTheNextRun)));

	TestEqual(TEXT("refused at difficulty tier 1"), AtOne, ECataclysmCityUpgradeResult::BelowItsDifficultyTier);
	TestEqual(TEXT("and at tier 2"), AtTwo, ECataclysmCityUpgradeResult::BelowItsDifficultyTier);
	TestEqual(TEXT("saying the tier it is offered from"),
			  UCataclysmCityUpgradeRules::ResultText(ECataclysmCityUpgradeResult::BelowItsDifficultyTier),
			  FString(TEXT("Available from difficulty tier 3.")));
	TestEqual(TEXT("offered at tier 3"), AtThree, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("and bought there with its build days"), Bought, ECataclysmCityUpgradeResult::Bought);
	TestTrue(TEXT("set-up: it is being built"), bBuilding);
	TestEqual(TEXT("another city is refused while it builds"), Again, ECataclysmCityUpgradeResult::AlreadyBoughtThisRun);
	TestEqual(TEXT("saying it was bought this run"),
			  UCataclysmCityUpgradeRules::ResultText(ECataclysmCityUpgradeResult::AlreadyBoughtThisRun),
			  FString(TEXT("Already bought this run.")));
	TestEqual(TEXT("CONTROL: another upgrade on that city is not refused"), Other, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("a new run may buy it again"), InTheNextRun, ECataclysmCityUpgradeResult::Bought);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityCleanseWaitsTest,
	"Cataclysm.CityCleanse.NothingHappensUntilItsBuildIsFinished",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Bought on the Pillar with its 6 build days, holding two dungeons that resolve in 100 and 200 days: after 5 days both
 * still stand and the Pillar holds the defence and population it held; on the sixth day the sooner one goes and half
 * of each is taken. THE PILLAR, because no surge targets it while it is sealed, so nothing else moves its figures;
 * asserted at the end. Prints "CLEANSE WAITS".
 */
bool FCataclysmCityCleanseWaitsTest::RunTest(const FString&)
{
	using namespace CataclysmCityCleanseTest;

	UCataclysmEmpireRun* Run = MakeClearedRun();
	const int32 PillarId = Run->Map->PillarId;
	PlaceDungeon(*Run, PillarId, 931, ECataclysmDungeonType::Basic, 100.0f);
	PlaceDungeon(*Run, PillarId, 932, ECataclysmDungeonType::Basic, 200.0f);
	const FCataclysmCity* Pillar = Run->Map->Find(PillarId);
	const float Defence = Run->Map->DefenceOf(*Pillar);
	const float Population = Run->Map->PopulationOf(*Pillar);

	const ECataclysmCityUpgradeResult Result =
		Run->BuyCityUpgrade(PillarId, Cleanse(), 0, 0, UCataclysmCityUpgradeRules::BuildDaysFor(1));
	Run->AdvanceDays(5);
	const bool bBothAfterFive = Stands(*Run, 931) && Stands(*Run, 932);
	const float DefenceAfterFive = Pillar->Defence;
	const float PopulationAfterFive = Pillar->Population;
	Run->AdvanceDay();

	AddInfo(FString::Printf(TEXT("CLEANSE WAITS: %s; after 5 days both stand %d, defence %.1f of %.1f, population %.1f "
								 "of %.1f; after 6, 931 %d 932 %d, defence %.1f, population %.1f; %d dungeons on the "
								 "Pillar"),
							*UCataclysmCityUpgradeRules::ResultText(Result), bBothAfterFive ? 1 : 0, DefenceAfterFive,
							Defence, PopulationAfterFive, Population, Stands(*Run, 931), Stands(*Run, 932),
							Pillar->Defence, Pillar->Population, Run->DungeonsOn(PillarId).Num()));

	TestEqual(TEXT("it is bought with 6 days to build"), Result, ECataclysmCityUpgradeResult::Bought);
	TestTrue(TEXT("after 5 days both dungeons stand"), bBothAfterFive);
	TestEqual(TEXT("and the Pillar holds its defence"), DefenceAfterFive, Defence, 0.01f);
	TestEqual(TEXT("and its population"), PopulationAfterFive, Population, 0.01f);
	TestFalse(TEXT("on the sixth day the sooner dungeon is removed"), Stands(*Run, 931));
	TestTrue(TEXT("and the later one stays"), Stands(*Run, 932));
	TestEqual(TEXT("and half the defence is taken"), Pillar->Defence, Defence * 0.5f, 0.01f);
	TestEqual(TEXT("and half the population"), Pillar->Population, Population * 0.5f, 0.01f);
	TestEqual(TEXT("CONTROL: no other dungeon came to the Pillar"), Run->DungeonsOn(PillarId).Num(), 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
