// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Player/CataclysmEmpireTree.h"

/**
 * The empire upgrade tree's nodes for a dungeon's walk and depth, and for a clear. Issue #2741 slice 4: the 19 nodes the
 * coordinating session's W1 to W9 of 2026-11-19 ruled on --
 *
 *   the walk    read where it is charged: the stored walk days, less the flat days (The Delver, Opportunist, Sovereign's
 *               Haste, Imperial Roads, Rapid Descent), times the share left (Pacing, Overclock, Temporal Mastery, Fleet
 *               Footed), halved by Tactical Entry above 50 floors, never below one day; a Cow Level not reduced; The
 *               Last Stand option one day on a city the hits scheduled over 7 days would fell
 *   the depth   added when a dungeon is made and counted in `UpgradeFloors`, so the timer grows and the hit does not:
 *               Deep Boring, Architect of Greed, Exclusionary Mapping, The Collector's Decree, Architectural Insight to
 *               +10, and Infinite Depths per active type but not on the boss
 *   a clear     The Warlord's 15% to remove the soonest other dungeon on the city; Veteran's Edge's +3% a point on the
 *               award
 *
 * `Cataclysm.EmpireTreeWalk.` IS A NEW GROUP and selects these and nothing else. A RUN MADE BY HAND, handed bonuses
 * through `UCataclysmEmpireRun::SetTreeBonuses`; the first two tests add the nodes up through the real tables.
 */
namespace CataclysmEmpireTreeWalkTest
{
	// The tree file's identifiers, by the node names the table gives them.
	const FName Pacing(TEXT("basic_00000029"));					// -2.5% walk a point, 10 points
	const FName Overclock(TEXT("basic_0000002m"));				// -2.5% walk a point, 20 points
	const FName TemporalMastery(TEXT("basic_0000002r"));		// -2.5% walk a point, 25 points
	const FName FleetFooted(TEXT("keystone_0000002b"));			// -12% walk
	const FName TacticalEntry(TEXT("keystone_0000002d"));		// halved above 50 floors
	const FName Opportunist(TEXT("basic_expl_t1e"));			// -1 day a point alone on its city, 5 points
	const FName SovereignsHaste(TEXT("keystone_0000002x"));		// -1 day a point per active type, to 30, 10 points
	const FName ImperialRoads(TEXT("basic_arch_t3d"));			// -1 day a point on a Fallen City dungeon, 10 points
	const FName RapidDescent(TEXT("basic_expl_t3e"));			// -0.1 day a point per floor cleared, 10 points
	const FName DeepBoring(TEXT("basic_0000002g"));				// +1 floor a point, 10 points
	const FName ArchitectOfGreed(TEXT("basic_0000002l"));		// +1 floor a point, 20 points
	const FName ExclusionaryMapping(TEXT("basic_0000002f"));	// -1 floor a point, 10 points
	const FName InfiniteDepths(TEXT("basic_expl_t4c"));			// +2 floors a point per active type, 10 points
	const FName ArchitecturalInsight(TEXT("basic_expl_t2g"));	// +1 floor a point per 10 Architect points, to +10
	const FName VeteransEdge(TEXT("basic_expl_t1d"));			// +3% empire points a point, 10 points
	const FName Foundation(TEXT("basic_0000000v"));				// an Architect node, for Architectural Insight's steps
	const FName Foundations(TEXT("capstone_0000000m"));			// option 2, The Delver: -5 days
	const FName Edicts(TEXT("capstone_0000000n"));				// option 2, The Collector's Decree: +10 floors
	const FName Vanguard(TEXT("capstone_0000000o"));			// option 1, The Warlord: 15%
	const FName Zenith(TEXT("capstone_0000000p"));				// option 2, The Last Stand: one day within 7 days

	FCataclysmEmpireTreeBonuses BonusesOf(const FCataclysmPassiveAllocation& Tree)
	{
		return UCataclysmEmpireTree::CityBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
													UCataclysmEmpireTree::LoadEffectTable(), Tree);
	}

	/** A run begun with these bonuses, its first day spent and its board emptied, as slice 3a's tests leave one. */
	UCataclysmEmpireRun* QuietRun(int32 Seed, const FCataclysmEmpireTreeBonuses& Bonuses = FCataclysmEmpireTreeBonuses())
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->SetTreeBonuses(Bonuses);
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	FCataclysmCity* FirstOf(UCataclysmEmpireRun& Run, ECataclysmCityTier Tier)
	{
		for (FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == Tier && !City.bFallen)
			{
				return &City;
			}
		}
		return nullptr;
	}

	/** Puts a dungeon on a city with a timer and a walk, as a surge would. */
	FCataclysmDungeon& Place(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type,
							 ECataclysmDungeonSubType SubType, const FCataclysmCity& City, float ResolveDays,
							 int32 Floors = 20, float WalkDays = 0.0f)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = Floors;
		Dungeon.WalkDays = WalkDays;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.SpawnedDay = Run.Day();
		Dungeon.DefenceDamage = 100.0f;
		Dungeon.PopulationDamage = 400.0f;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
		return Run.Dungeons.Last();
	}

	/** A dungeon not on the map, for asking the walk of. */
	FCataclysmDungeon Walk(int32 Floors, float WalkDays, ECataclysmDungeonType Type = ECataclysmDungeonType::Basic,
						   ECataclysmDungeonSubType SubType = ECataclysmDungeonSubType::None)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = 9900;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.CityId = INDEX_NONE;
		Dungeon.Floors = Floors;
		Dungeon.WalkDays = WalkDays;
		return Dungeon;
	}
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

/**
 * The walk and depth nodes add up through the generated tables: Pacing, Overclock and Temporal Mastery at their 55
 * points and Fleet Footed leave 0.975^55 x 0.88 of a walk, the simulation's `run_days_mult`; Tactical Entry halves
 * above 50; The Delver 5 days; Opportunist 5; Sovereign's Haste 10 a type to 30; Imperial Roads 10; Rapid Descent 1 a
 * floor; The Last Stand 7 days; Deep Boring, Architect of Greed, Exclusionary Mapping and The Collector's Decree +30
 * floors; Infinite Depths +20 a type; The Warlord 15%; Veteran's Edge +30%.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkTableTest,
	"Cataclysm.EmpireTreeWalk.TheNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	if (!TestNotNull(TEXT("set-up: DT_EmpireTreeEffects"), UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(Pacing, 10);
	Tree.Add(Overclock, 20);
	Tree.Add(TemporalMastery, 25);
	Tree.Add(FleetFooted, 1);
	Tree.Add(TacticalEntry, 1);
	Tree.Add(Opportunist, 5);
	Tree.Add(SovereignsHaste, 10);
	Tree.Add(ImperialRoads, 10);
	Tree.Add(RapidDescent, 10);
	Tree.Add(DeepBoring, 10);
	Tree.Add(ArchitectOfGreed, 20);
	Tree.Add(ExclusionaryMapping, 10);
	Tree.Add(InfiniteDepths, 10);
	Tree.Add(VeteransEdge, 10);
	for (const FName Capstone : { Foundations, Edicts, Vanguard, Zenith })
	{
		Tree.Add(Capstone, 1);
	}
	Tree.SetChosenOption(Foundations, 2);
	Tree.SetChosenOption(Edicts, 2);
	Tree.SetChosenOption(Vanguard, 1);
	Tree.SetChosenOption(Zenith, 2);
	const FCataclysmEmpireTreeBonuses Held = BonusesOf(Tree);
	const float SimulationsShare = FMath::Pow(0.975f, 55.0f) * 0.88f;

	AddInfo(FString::Printf(TEXT("WALK TABLE: share %.4f (simulation %.4f), halve above %d, Delver %.1f, alone %.1f, per type "
								 "%.1f to %.1f, Fallen City %.1f, per floor %.2f, Last Stand %d; floors %d and %d a type; "
								 "Warlord %.2f; Veteran %.2f"),
							Held.WalkTimeFactor, SimulationsShare, Held.HalveWalkAboveFloors, Held.WalkDaysFewer,
							Held.WalkDaysFewerWhenAlone, Held.WalkDaysFewerPerActiveType,
							Held.WalkDaysFewerPerActiveTypeCap, Held.WalkDaysFewerOnFallenCities,
							Held.WalkDaysFewerPerFloorCleared, Held.OneDayWalkWithinDaysOfFalling, Held.FloorsAdded,
							Held.FloorsAddedPerActiveType, Held.RemoveAnotherDungeonChance, Held.EmpirePointsIncrease));
	TestEqual(TEXT("the share left is the simulation's run_days_mult"), Held.WalkTimeFactor, SimulationsShare, 0.0001f);
	TestEqual(TEXT("Tactical Entry above 50 floors"), Held.HalveWalkAboveFloors, 50);
	TestEqual(TEXT("The Delver: 5 days"), Held.WalkDaysFewer, 5.0f, 0.0001f);
	TestEqual(TEXT("Opportunist at 5 points: 5 days"), Held.WalkDaysFewerWhenAlone, 5.0f, 0.0001f);
	TestEqual(TEXT("Sovereign's Haste at 10 points: 10 a type"), Held.WalkDaysFewerPerActiveType, 10.0f, 0.0001f);
	TestEqual(TEXT("to 30"), Held.WalkDaysFewerPerActiveTypeCap, 30.0f, 0.0001f);
	TestEqual(TEXT("Imperial Roads at 10 points: 10 days"), Held.WalkDaysFewerOnFallenCities, 10.0f, 0.0001f);
	TestEqual(TEXT("Rapid Descent at 10 points: 1 day a floor"), Held.WalkDaysFewerPerFloorCleared, 1.0f, 0.0001f);
	TestEqual(TEXT("The Last Stand: 7 days"), Held.OneDayWalkWithinDaysOfFalling, 7);
	TestEqual(TEXT("+10 +20 -10 +10 floors"), Held.FloorsAdded, 30);
	TestEqual(TEXT("Infinite Depths at 10 points: 20 a type"), Held.FloorsAddedPerActiveType, 20);
	TestEqual(TEXT("The Warlord: 15%"), Held.RemoveAnotherDungeonChance, 0.15f, 0.0001f);
	TestEqual(TEXT("Veteran's Edge at 10 points: +30%"), Held.EmpirePointsIncrease, 0.30f, 0.0001f);
	return true;
}

/**
 * Architectural Insight counts whole 10-point steps of the Architect branch, per point, held at +10 floors (W4): 10
 * points with 9 Architect points add nothing, with 20 add 20 and are held at 10, and 1 point with 50 adds 5.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkInsightTest,
	"Cataclysm.EmpireTreeWalk.ArchitecturalInsightCountsArchitectStepsAndStopsAtItsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkInsightTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	auto Insight = [](int32 Points, int32 ArchitectPoints)
	{
		FCataclysmPassiveAllocation Tree;
		Tree.Add(ArchitecturalInsight, Points);
		Tree.Add(Foundation, ArchitectPoints); // One node holding them all: only the branch's total is counted.
		return BonusesOf(Tree).FloorsAdded;
	};
	const int32 NineArchitect = Insight(10, 9);
	const int32 TwentyArchitect = Insight(10, 20);
	const int32 OnePoint = Insight(1, 50);

	AddInfo(FString::Printf(TEXT("INSIGHT: 10 points with 9 / 20 Architect points +%d / +%d floors; 1 point with 50 +%d"),
							NineArchitect, TwentyArchitect, OnePoint));
	TestEqual(TEXT("9 Architect points is no step"), NineArchitect, 0);
	TestEqual(TEXT("10 points at 2 steps would be 20 and is held at 10"), TwentyArchitect, 10);
	TestEqual(TEXT("1 point at 5 steps: 5"), OnePoint, 5);
	return true;
}

// ---------------------------------------------------------------------------
// The walk
// ---------------------------------------------------------------------------

/**
 * With no walk node the run charges exactly the stored figure: a dungeon storing 7.5 days over 10 floors walks 0.75 a
 * floor, and one storing none walks one day a floor, as `FCataclysmDungeon::WalkDaysPerFloor` charges.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkNoneTest,
	"Cataclysm.EmpireTreeWalk.AWalkWithNoNodeIsChargedTheStoredFigure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkNoneTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	const FCataclysmDungeon Stored = Walk(10, 7.5f);
	const FCataclysmDungeon None = Walk(12, 0.0f);

	AddInfo(FString::Printf(TEXT("NO NODE: %.3f and %.3f a floor; the dungeon's own rate %.3f and %.3f"),
							Run->WalkDaysPerFloorFor(Stored), Run->WalkDaysPerFloorFor(None), Stored.WalkDaysPerFloor(),
							None.WalkDaysPerFloor()));
	TestEqual(TEXT("7.5 days over 10 floors: 0.75 a floor"), Run->WalkDaysPerFloorFor(Stored), Stored.WalkDaysPerFloor(),
			  0.00001f);
	TestEqual(TEXT("none stored: one day a floor"), Run->WalkDaysPerFloorFor(None), None.WalkDaysPerFloor(), 0.00001f);
	TestEqual(TEXT("and 12 days in all"), Run->EffectiveWalkDaysFor(None), 12.0f, 0.00001f);
	return true;
}

/**
 * The share left and Tactical Entry: at half, a 20-floor walk of 20 days is 10; a 60-floor walk of 60 is 30 and then
 * halved to 15; a 50-floor walk of 50 is 25, not "more than 50"; a Cow Level of 40 is not reduced.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkShareTest,
	"Cataclysm.EmpireTreeWalk.TheShareNodesAndTacticalEntryShortenAWalkAndNotACowLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkShareTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Bonuses;
	Bonuses.WalkTimeFactor = 0.5f;
	Bonuses.HalveWalkAboveFloors = 50;
	UCataclysmEmpireRun* Run = QuietRun(2741, Bonuses);
	const float Twenty = Run->EffectiveWalkDaysFor(Walk(20, 20.0f));
	const float Sixty = Run->EffectiveWalkDaysFor(Walk(60, 60.0f));
	const float Fifty = Run->EffectiveWalkDaysFor(Walk(50, 50.0f));
	const float Cow = Run->EffectiveWalkDaysFor(
		Walk(20, 40.0f, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::CowLevel));

	AddInfo(FString::Printf(TEXT("SHARE: 20 floors %.2f, 60 floors %.2f, 50 floors %.2f, a Cow Level %.2f"), Twenty, Sixty,
							Fifty, Cow));
	TestEqual(TEXT("half of 20 days"), Twenty, 10.0f, 0.0001f);
	TestEqual(TEXT("half of 60, halved again above 50 floors"), Sixty, 15.0f, 0.0001f);
	TestEqual(TEXT("50 floors is not more than 50"), Fifty, 25.0f, 0.0001f);
	TestEqual(TEXT("a Cow Level cannot be reduced"), Cow, 40.0f, 0.0001f);
	return true;
}

/**
 * The flat days come before the share, and the one-day minimum holds (W1): The Delver's 5 off 20 days at half is 7.5;
 * off 6 days at half it would be 0.5 and is 1.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkOrderTest,
	"Cataclysm.EmpireTreeWalk.TheFlatDaysComeBeforeTheShareAndTheWalkIsNeverBelowADay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkOrderTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Bonuses;
	Bonuses.WalkDaysFewer = 5.0f;
	Bonuses.WalkTimeFactor = 0.5f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Bonuses);
	const float Long = Run->EffectiveWalkDaysFor(Walk(20, 20.0f));
	const float Short = Run->EffectiveWalkDaysFor(Walk(6, 6.0f));

	AddInfo(FString::Printf(TEXT("ORDER: 20 days to %.2f, 6 days to %.2f"), Long, Short));
	TestEqual(TEXT("(20 - 5) x 0.5"), Long, 7.5f, 0.0001f);
	TestEqual(TEXT("(6 - 5) x 0.5 is held at a day"), Short, 1.0f, 0.0001f);
	return true;
}

/**
 * The conditional flat days: Opportunist's 5 only for a dungeon alone on its city (W9); Sovereign's Haste's 10 a type,
 * 20 for two active types and held at 30 for four (W8); Imperial Roads' 10 only on a Fallen City dungeon; Rapid
 * Descent's 1 a floor for each floor before the last (W2).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkConditionsTest,
	"Cataclysm.EmpireTreeWalk.TheConditionalDaysReadTheirConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkConditionsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	// OPPORTUNIST.
	FCataclysmEmpireTreeBonuses Alone;
	Alone.WalkDaysFewerWhenAlone = 5.0f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Alone);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
	{
		return false;
	}
	const FCataclysmDungeon Lone = Place(*Run, 9001, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark,
										 50.0f, 20, 20.0f);
	const float WhenAlone = Run->EffectiveWalkDaysFor(Lone);
	Place(*Run, 9002, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 50.0f, 20, 20.0f);
	const float WithAnother = Run->EffectiveWalkDaysFor(Run->Dungeons[0]);

	// SOVEREIGN'S HASTE.
	FCataclysmEmpireTreeBonuses Haste;
	Haste.WalkDaysFewerPerActiveType = 10.0f;
	Haste.WalkDaysFewerPerActiveTypeCap = 30.0f;
	UCataclysmEmpireRun* Hasty = QuietRun(2741, Haste);
	Hasty->ActiveCataclysms = { ECataclysmType::War, ECataclysmType::Famine };
	const float TwoTypes = Hasty->EffectiveWalkDaysFor(Walk(50, 50.0f));
	Hasty->ActiveCataclysms = { ECataclysmType::War, ECataclysmType::Famine, ECataclysmType::Pestilence,
								ECataclysmType::Void };
	const float FourTypes = Hasty->EffectiveWalkDaysFor(Walk(50, 50.0f));

	// IMPERIAL ROADS.
	FCataclysmEmpireTreeBonuses Roads;
	Roads.WalkDaysFewerOnFallenCities = 10.0f;
	UCataclysmEmpireRun* Roaded = QuietRun(2741, Roads);
	const float Fallen = Roaded->EffectiveWalkDaysFor(Walk(15, 0.0f, ECataclysmDungeonType::FallenCity));
	const float Ordinary = Roaded->EffectiveWalkDaysFor(Walk(15, 15.0f));

	// RAPID DESCENT.
	FCataclysmEmpireTreeBonuses Descent;
	Descent.WalkDaysFewerPerFloorCleared = 1.0f;
	UCataclysmEmpireRun* Descending = QuietRun(2741, Descent);
	const float TenFloors = Descending->EffectiveWalkDaysFor(Walk(10, 30.0f));
	const float TwentyFloors = Descending->EffectiveWalkDaysFor(Walk(20, 20.0f));

	AddInfo(FString::Printf(TEXT("CONDITIONS: alone %.1f, with another %.1f; two types %.1f, four %.1f; Fallen City %.1f, "
								 "ordinary %.1f; 10 floors of 30 days %.1f, 20 of 20 %.1f"),
							WhenAlone, WithAnother, TwoTypes, FourTypes, Fallen, Ordinary, TenFloors, TwentyFloors));
	TestEqual(TEXT("Opportunist: alone, 20 - 5"), WhenAlone, 15.0f, 0.0001f);
	TestEqual(TEXT("with another dungeon on the city, nothing"), WithAnother, 20.0f, 0.0001f);
	TestEqual(TEXT("Sovereign's Haste, two types: 50 - 20"), TwoTypes, 30.0f, 0.0001f);
	TestEqual(TEXT("four types: 40, held at 30"), FourTypes, 20.0f, 0.0001f);
	TestEqual(TEXT("Imperial Roads on a Fallen City dungeon: 15 - 10"), Fallen, 5.0f, 0.0001f);
	TestEqual(TEXT("and not on an ordinary one"), Ordinary, 15.0f, 0.0001f);
	TestEqual(TEXT("Rapid Descent: 30 - 9 floors before the last"), TenFloors, 21.0f, 0.0001f);
	TestEqual(TEXT("20 - 19 floors before the last"), TwentyFloors, 1.0f, 0.0001f);
	return true;
}

/**
 * The Last Stand option (W5): an Outpost holding 50 with an ordinary dungeon resolving in 3 days is within 7 days of
 * falling, so a walk of a dungeon on it is one day; with that dungeon's timer at 9 days it is not, and the walk is its
 * 20. The scheduled loss counts a Quest timer inside 7 days at the ordinary row's 70, and 7 days of a Siege's share at
 * 10 a day plus 2.5 for each day it has stood: 140.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkLastStandTest,
	"Cataclysm.EmpireTreeWalk.TheLastStandOptionWalksADungeonOnAFallingCityInOneDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkLastStandTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses LastStand;
	LastStand.OneDayWalkWithinDaysOfFalling = 7;
	LastStand.WalkDaysFewer = 0.0f;
	UCataclysmEmpireRun* Soon = QuietRun(2741, LastStand);
	UCataclysmEmpireRun* Later = QuietRun(2741, LastStand);
	UCataclysmEmpireRun* Counted = QuietRun(2741, LastStand);
	FCataclysmCity* SoonCity = FirstOf(*Soon, ECataclysmCityTier::Outpost);
	FCataclysmCity* LaterCity = FirstOf(*Later, ECataclysmCityTier::Outpost);
	FCataclysmCity* CountedCity = FirstOf(*Counted, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: an Outpost in each run"), SoonCity && LaterCity && CountedCity))
	{
		return false;
	}
	SoonCity->Defence = 50.0f;
	LaterCity->Defence = 50.0f;
	const FCataclysmDungeon Walked = Place(*Soon, 9001, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None,
										   *SoonCity, 50.0f, 20, 20.0f);
	Place(*Soon, 9002, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *SoonCity, 3.0f);
	const float SoonWalk = Soon->EffectiveWalkDaysFor(Walked);
	const FCataclysmDungeon NotYet = Place(*Later, 9001, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None,
										   *LaterCity, 50.0f, 20, 20.0f);
	Place(*Later, 9002, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *LaterCity, 9.0f);
	const float LaterWalk = Later->EffectiveWalkDaysFor(NotYet);

	const int32 CountedId = CountedCity->CityId;
	Place(*Counted, 9003, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::None, *CountedCity, 2.0f);
	const float QuestLoss = Counted->ScheduledDefenceLossOn(CountedId, 7);
	Counted->Dungeons.Empty();
	Counted->Clock->Timers.Empty();
	Place(*Counted, 9004, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, *CountedCity, 10000.0f);
	const float SiegeLoss = Counted->ScheduledDefenceLossOn(CountedId, 7);
	const float QuestRow = UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Basic,
															 ECataclysmCityTier::Outpost).DefenceDamage;

	AddInfo(FString::Printf(TEXT("LAST STAND: a hit in 3 days walks %.1f, in 9 days %.1f; a Quest timer counts %.1f (the row "
								 "%.1f); a Siege's 7 days count %.1f"),
							SoonWalk, LaterWalk, QuestLoss, QuestRow, SiegeLoss));
	TestEqual(TEXT("a city a scheduled hit fells inside 7 days: one day"), SoonWalk, 1.0f, 0.0001f);
	TestEqual(TEXT("CONTROL: the hit 9 days off: the walk's own 20"), LaterWalk, 20.0f, 0.0001f);
	TestEqual(TEXT("a Quest timer inside 7 days counts the ordinary row"), QuestLoss, QuestRow, 0.01f);
	TestEqual(TEXT("a Siege's 7 days: 7 x 10 + 2.5 x (1 + ... + 7)"), SiegeLoss, 140.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// The depth
// ---------------------------------------------------------------------------

/**
 * Floors are added when a dungeon is made and counted in `UpgradeFloors` (W3): two runs on one seed, one adding 30, fire
 * the same surge; each of its dungeons has 30 more floors in the second, the same `BiteScale`, and a longer timer. The
 * tree takes no draw, so the two waves are the same dungeons.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkFloorsTest,
	"Cataclysm.EmpireTreeWalk.FloorsAreAddedWhenADungeonIsMadeAndNotToItsHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkFloorsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Deeper;
	Deeper.FloorsAdded = 30;
	UCataclysmEmpireRun* Plain = QuietRun(2741);
	UCataclysmEmpireRun* Deep = QuietRun(2741, Deeper);
	for (UCataclysmEmpireRun* Run : { Plain, Deep })
	{
		Run->Surges->NextSurgeDay = static_cast<float>(Run->Day() + 1);
		Run->AdvanceDay();
	}
	if (!TestTrue(TEXT("set-up: both surges landed the same number of dungeons"),
				  Plain->Dungeons.Num() > 0 && Plain->Dungeons.Num() == Deep->Dungeons.Num()))
	{
		return false;
	}

	int32 ThirtyMore = 0;
	int32 SameHit = 0;
	int32 LongerTimer = 0;
	for (int32 Index = 0; Index < Plain->Dungeons.Num(); ++Index)
	{
		const FCataclysmDungeon& A = Plain->Dungeons[Index];
		const FCataclysmDungeon& B = Deep->Dungeons[Index];
		ThirtyMore += (B.Floors - A.Floors == 30 && B.UpgradeFloors - A.UpgradeFloors == 30) ? 1 : 0;
		SameHit += FMath::IsNearlyEqual(A.BiteScale(), B.BiteScale()) ? 1 : 0;
		LongerTimer += B.ResolveDays > A.ResolveDays ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("FLOORS: %d dungeons; %d with 30 more floors counted as added, %d with the same hit, %d "
								 "with a longer timer"),
							Plain->Dungeons.Num(), ThirtyMore, SameHit, LongerTimer));
	TestEqual(TEXT("every dungeon has 30 more floors, counted as added"), ThirtyMore, Plain->Dungeons.Num());
	TestEqual(TEXT("and the same hit"), SameHit, Plain->Dungeons.Num());
	TestEqual(TEXT("and a longer timer"), LongerTimer, Plain->Dungeons.Num());
	return true;
}

/**
 * Infinite Depths adds its floors for each active type, and fewer floors never go below one: a run facing its types
 * with 2 a type adds 2 x that many; a tree taking 500 floors away leaves every dungeon one floor.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkPerTypeTest,
	"Cataclysm.EmpireTreeWalk.InfiniteDepthsCountsActiveTypesAndFloorsNeverGoBelowOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkPerTypeTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Depths;
	Depths.FloorsAddedPerActiveType = 2;
	FCataclysmEmpireTreeBonuses Shallow;
	Shallow.FloorsAdded = -500;
	UCataclysmEmpireRun* Plain = QuietRun(2741);
	UCataclysmEmpireRun* Deep = QuietRun(2741, Depths);
	UCataclysmEmpireRun* Flat = QuietRun(2741, Shallow);
	for (UCataclysmEmpireRun* Run : { Plain, Deep, Flat })
	{
		Run->Surges->NextSurgeDay = static_cast<float>(Run->Day() + 1);
		Run->AdvanceDay();
	}
	const int32 Types = Deep->ActiveCataclysms.Num();
	const int32 Added = Deep->Dungeons.Num() > 0 && Plain->Dungeons.Num() > 0
		? Deep->Dungeons[0].Floors - Plain->Dungeons[0].Floors : -1;
	int32 OneFloor = 0;
	for (const FCataclysmDungeon& Dungeon : Flat->Dungeons)
	{
		OneFloor += Dungeon.Floors == 1 ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("PER TYPE: %d active types add %d floors; %d of %d dungeons at one floor"), Types, Added,
							OneFloor, Flat->Dungeons.Num()));
	TestTrue(TEXT("set-up: the run faces at least one type"), Types > 0);
	TestEqual(TEXT("2 floors for each active type"), Added, 2 * Types);
	TestTrue(TEXT("set-up: a surge landed"), Flat->Dungeons.Num() > 0);
	TestEqual(TEXT("500 floors fewer leaves every dungeon one floor"), OneFloor, Flat->Dungeons.Num());
	return true;
}

/**
 * A floor node reaches only dungeons made after it (W3): a run's standing dungeons keep their floors when the tree is
 * taken away, and a Fallen City dungeon made with 30 added floors has its depth plus 30.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkStandingTest,
	"Cataclysm.EmpireTreeWalk.AFloorNodeReachesOnlyDungeonsMadeAfterIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkStandingTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Deeper;
	Deeper.FloorsAdded = 30;
	UCataclysmEmpireRun* Run = QuietRun(2741, Deeper);
	Run->Surges->NextSurgeDay = static_cast<float>(Run->Day() + 1);
	Run->AdvanceDay();
	TArray<int32> Before;
	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		Before.Add(Dungeon.Floors);
	}
	Run->SetTreeBonuses(FCataclysmEmpireTreeBonuses());
	TArray<int32> After;
	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		After.Add(Dungeon.Floors);
	}

	const FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost))
	{
		return false;
	}
	const int32 Least = UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::FallenCity, Outpost->Tier).LeastFloors;
	const FCataclysmDungeon Fallen = UCataclysmSurgeScheduler::MakeFallenCityDungeon(9, *Outpost, 40, 1, 30);
	const FCataclysmDungeon Bare = UCataclysmSurgeScheduler::MakeFallenCityDungeon(9, *Outpost, 40, 1);

	AddInfo(FString::Printf(TEXT("STANDING: %d dungeons keep their floors %s; a Fallen City dungeon %d floors with 30 added, "
								 "%d without (least %d)"),
							Before.Num(), Before == After ? TEXT("yes") : TEXT("NO"), Fallen.Floors, Bare.Floors, Least));
	TestTrue(TEXT("set-up: a surge landed"), Before.Num() > 0);
	TestTrue(TEXT("standing dungeons keep the floors they were made with"), Before == After);
	TestEqual(TEXT("a Fallen City dungeon made with 30 added: its depth plus 30"), Fallen.Floors, Bare.Floors + 30);
	return true;
}

/**
 * The boss gets the flat floors and not Infinite Depths (W3): opened at 100 floors with nothing defeated, it is 130 with
 * 30 flat floors and 20 a type for Infinite Depths, and never below one with 500 fewer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkBossTest,
	"Cataclysm.EmpireTreeWalk.TheBossGetsTheFlatFloorsAndNotInfiniteDepths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkBossTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Both;
	Both.FloorsAdded = 30;
	Both.FloorsAddedPerActiveType = 20;
	UCataclysmEmpireRun* Run = QuietRun(2741, Both);
	Run->bCataclysmDungeonOpened = true;
	Run->CataclysmDungeonBaseFloors = 100;
	Run->BasicDungeonsCleared = 0;
	const int32 WithTheTree = Run->CataclysmDungeonFloorsNow();
	FCataclysmEmpireTreeBonuses Shallow;
	Shallow.FloorsAdded = -500;
	Run->SetTreeBonuses(Shallow);
	const int32 Shallowest = Run->CataclysmDungeonFloorsNow();

	AddInfo(FString::Printf(TEXT("BOSS: 100 floors with the tree %d; with 500 fewer %d"), WithTheTree, Shallowest));
	TestEqual(TEXT("100 + 30, and no Infinite Depths"), WithTheTree, 130);
	TestEqual(TEXT("never below one"), Shallowest, 1);
	return true;
}

// ---------------------------------------------------------------------------
// A clear
// ---------------------------------------------------------------------------

/**
 * The Warlord at a certain chance removes the other dungeon on the city that resolves soonest, never a Fallen City
 * dungeon, through `RemoveDungeon`: the clear counts once and the run's stream does not move. CONTROL: at no chance
 * nothing is removed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkWarlordTest,
	"Cataclysm.EmpireTreeWalk.TheWarlordRemovesTheSoonestOtherDungeonOnTheCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkWarlordTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Certain;
	Certain.RemoveAnotherDungeonChance = 1.0f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Certain);
	UCataclysmEmpireRun* Control = QuietRun(2741);
	int32 Removed[2] = { INDEX_NONE, INDEX_NONE };
	int32 Left[2] = { 0, 0 };
	int32 Cleared[2] = { 0, 0 };
	int32 StreamBefore = 0;
	int32 StreamAfter = 0;
	UCataclysmEmpireRun* Runs[2] = { Run, Control };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UCataclysmEmpireRun& Each = *Runs[Index];
		const FCataclysmCity* Bulwark = FirstOf(Each, ECataclysmCityTier::Bulwark);
		if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
		{
			return false;
		}
		const FCataclysmCity City = *Bulwark;
		Place(Each, 9001, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, City, 30.0f);
		Place(Each, 9002, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, City, 10.0f);
		Place(Each, 9003, ECataclysmDungeonType::FallenCity, ECataclysmDungeonSubType::None, City, 5.0f);
		Place(Each, 9004, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, City, 50.0f);
		if (Index == 0)
		{
			StreamBefore = Each.Stream.GetCurrentSeed();
		}
		Each.ClearDungeon(9004);
		if (Index == 0)
		{
			StreamAfter = Each.Stream.GetCurrentSeed();
		}
		Removed[Index] = Each.LastDungeonTheWarlordRemoved;
		Left[Index] = Each.Dungeons.Num();
		Cleared[Index] = Each.DungeonsCleared;
	}

	AddInfo(FString::Printf(TEXT("WARLORD: removed %d, %d left, %d cleared, stream %s; at no chance removed %d, %d left"),
							Removed[0], Left[0], Cleared[0], StreamBefore == StreamAfter ? TEXT("unmoved") : TEXT("MOVED"),
							Removed[1], Left[1]));
	TestEqual(TEXT("the soonest other dungeon that is not a Fallen City's"), Removed[0], 9002);
	TestEqual(TEXT("two left: the 30-day one and the Fallen City's"), Left[0], 2);
	TestEqual(TEXT("the clear counted once"), Cleared[0], 1);
	TestEqual(TEXT("and the run's stream did not move"), StreamAfter, StreamBefore);
	TestEqual(TEXT("CONTROL: at no chance nothing is removed"), Removed[1], static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("CONTROL: three left"), Left[1], 3);
	return true;
}

/**
 * The Warlord at its 15% removes another dungeon at about that rate, and the same run rolls the same: 100 clears, each
 * beside one other dungeon, remove between 5 and 30 of them, and a second run on the same seed removes the same ones.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkWarlordRateTest,
	"Cataclysm.EmpireTreeWalk.TheWarlordsRollIsAboutItsChanceAndRollsTheSameAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkWarlordRateTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Warlord;
	Warlord.RemoveAnotherDungeonChance = 0.15f;
	TArray<int32> Removals[2];
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		UCataclysmEmpireRun* Run = QuietRun(2741, Warlord);
		const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
		if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
		{
			return false;
		}
		const FCataclysmCity City = *Bulwark;
		for (int32 Clear = 0; Clear < 100; ++Clear)
		{
			const int32 Cleared = 10000 + 2 * Clear;
			Place(*Run, Cleared, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, City, 50.0f);
			Place(*Run, Cleared + 1, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, City, 40.0f);
			Run->ClearDungeon(Cleared);
			if (Run->LastDungeonTheWarlordRemoved == Cleared + 1)
			{
				Removals[Pass].Add(Cleared);
			}
			else
			{
				Run->ClearDungeon(Cleared + 1);
			}
		}
	}

	AddInfo(FString::Printf(TEXT("WARLORD RATE: %d of 100 clears removed another; again %d, the same %s"),
							Removals[0].Num(), Removals[1].Num(), Removals[0] == Removals[1] ? TEXT("yes") : TEXT("NO")));
	TestTrue(TEXT("between 5 and 30 of 100 at 15%"), Removals[0].Num() >= 5 && Removals[0].Num() <= 30);
	TestTrue(TEXT("and the same run rolls the same"), Removals[0] == Removals[1]);
	return true;
}

/**
 * Veteran's Edge at 10 points pays 1.3 times a clear's award (W7): two runs on one seed clear the same dungeon.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWalkVeteranTest,
	"Cataclysm.EmpireTreeWalk.VeteransEdgeRaisesAClearsAward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWalkVeteranTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeWalkTest;

	FCataclysmEmpireTreeBonuses Veteran;
	Veteran.EmpirePointsIncrease = 0.3f;
	UCataclysmEmpireRun* Plain = QuietRun(2741);
	UCataclysmEmpireRun* Raised = QuietRun(2741, Veteran);
	double Awards[2] = { 0.0, 0.0 };
	UCataclysmEmpireRun* Runs[2] = { Plain, Raised };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FCataclysmCity* Bulwark = FirstOf(*Runs[Index], ECataclysmCityTier::Bulwark);
		if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
		{
			return false;
		}
		Place(*Runs[Index], 9001, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 50.0f);
		Runs[Index]->ClearDungeon(9001);
		Awards[Index] = Runs[Index]->LastEmpirePointsAwarded;
	}

	AddInfo(FString::Printf(TEXT("VETERAN: %.4f without, %.4f with"), Awards[0], Awards[1]));
	TestTrue(TEXT("set-up: the clear paid something"), Awards[0] > 0.0);
	TestEqual(TEXT("1.3 times the award"), Awards[1], Awards[0] * 1.3, 0.0001);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
