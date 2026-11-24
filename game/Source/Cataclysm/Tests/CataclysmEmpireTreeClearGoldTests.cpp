// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Items/CataclysmDropRoll.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * The empire upgrade tree's slice 6b: the five gold nodes that need the run record. Issue #2741, the coordinating
 * session's B1 to B5 of 2026-11-20, at run record version 12:
 *
 *   Price Controls     -4% a point on an upgrade bought on a city a dungeon has damaged this run (B1), by the city's
 *                      saved mark, which `UCataclysmEmpireMap::Damage` sets
 *   Stimulus Package   a kill's gold doubled while the run's population has fallen below half since the last reclaim
 *                      (B2), its own "more"
 *   Cleanup Crew       at a clear, 10% of the walk's gold dropped and not picked up, rounded down (B3)
 *   Salvage Rights     at a clear, 50 tier 1 gold a point for every floor, scaled to the tier (B4, R10)
 *   Investment Funds   +10% a point on that payout (B5), beside the increases G2 sends to it
 *
 * `Cataclysm.EmpireTreeClearGold.` IS A NEW GROUP and selects these and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeClearGoldTest
{
	const FName PriceControls(TEXT("basic_tres_t2f"));			// -4% a point, 10 points
	const FName StimulusPackage(TEXT("keystone_00000049"));		// doubled, a keystone
	const FName CleanupCrew(TEXT("keystone_0000003t"));			// 10%, a keystone
	const FName SalvageRights(TEXT("basic_0000003z"));			// 50 gold a point a floor, 20 points
	const FName InvestmentFunds(TEXT("basic_tres_t2c"));		// +10% a point, 10 points

	FCataclysmEmpireTreePlayerBonuses BonusesOf(const FCataclysmPassiveAllocation& Tree)
	{
		return UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
													  UCataclysmEmpireTree::LoadEffectTable(), Tree);
	}

	/** A run begun on this seed, its first day spent and its board emptied, as the earlier slices' tests leave one. */
	UCataclysmEmpireRun* QuietClearRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** A dungeon of this kind on this city, with its timer, as a surge would put one. */
	void PlaceOn(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type, const FCataclysmCity& City)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 20;
		Dungeon.ResolveDays = 50.0f;
		Dungeon.SpawnedDay = Run.Day();
		Dungeon.DefenceDamage = 100.0f;
		Dungeon.PopulationDamage = 400.0f;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, Dungeon.ResolveDays);
	}

	/** A run file read from its text, or null after saying why. */
	UCataclysmRunSave* ReadRunText(FAutomationTestBase& Test, const FString& Text, const TCHAR* Name)
	{
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(Result), *Message));
		}
		return Read;
	}
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

/**
 * The five add up through the generated tables at full points: Price Controls 0.40, Stimulus Package 1.00 (doubled),
 * Cleanup Crew 0.10, Salvage Rights 1000 tier 1 gold a floor (50 a point, 20 points), Investment Funds 1.00. CONTROL: an
 * empty tree gives none of them. Prints "CLEAR GOLD TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldTableTest,
	"Cataclysm.EmpireTreeClearGold.TheFiveNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeClearGoldTest;

	if (!TestNotNull(TEXT("set-up: the node table"), UCataclysmEmpireTree::LoadNodeTable())
		|| !TestNotNull(TEXT("set-up: the effect table"), UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(PriceControls, 10);
	Tree.Add(StimulusPackage, 1);
	Tree.Add(CleanupCrew, 1);
	Tree.Add(SalvageRights, 20);
	Tree.Add(InvestmentFunds, 10);
	const FCataclysmEmpireTreePlayerBonuses All = BonusesOf(Tree);
	const FCataclysmEmpireTreePlayerBonuses None = BonusesOf(FCataclysmPassiveAllocation());
	AddInfo(FString::Printf(TEXT("CLEAR GOLD TABLE: price %.2f, stimulus %.2f, cleanup %.2f, salvage %.0f a floor, "
								 "investment %.2f"),
							All.SurgeDamagedCityUpgradePriceReduced, All.GoldDoubledBelowHalfPopulation,
							All.UnpickedGoldShareAtClear, All.GoldPerFloorAtClear, All.ClearPayoutIncreased));

	TestEqual(TEXT("Price Controls: 0.40"), All.SurgeDamagedCityUpgradePriceReduced, 0.40f, 1.0e-4f);
	TestEqual(TEXT("Stimulus Package: 1.00, doubled"), All.GoldDoubledBelowHalfPopulation, 1.0f, 1.0e-4f);
	TestEqual(TEXT("Cleanup Crew: 0.10"), All.UnpickedGoldShareAtClear, 0.10f, 1.0e-4f);
	TestEqual(TEXT("Salvage Rights: 1000 a floor"), All.GoldPerFloorAtClear, 1000.0f, 1.0e-2f);
	TestEqual(TEXT("Investment Funds: 1.00"), All.ClearPayoutIncreased, 1.0f, 1.0e-4f);
	TestTrue(TEXT("CONTROL: an empty tree gives none"),
			 None.SurgeDamagedCityUpgradePriceReduced == 0.0f && None.GoldDoubledBelowHalfPopulation == 0.0f
				 && None.UnpickedGoldShareAtClear == 0.0f && None.GoldPerFloorAtClear == 0.0f
				 && None.ClearPayoutIncreased == 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Price Controls (B1)
// ---------------------------------------------------------------------------

/**
 * A bite marks its city and Price Controls reads the mark (B1): city 2 bitten for 10 defence is marked; city 3 hit for
 * nothing is not; city 4, whose defence is set below its maximum with no bite -- what a maximum the tree raised looks
 * like -- is not. With Price Controls' 0.40, an upgrade on the marked city, which holds one already, costs 0.60; on the
 * other two 1.00. Prints "CLEAR GOLD MARK".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldMarkTest,
	"Cataclysm.EmpireTreeClearGold.ADungeonsBiteMarksTheCityAndPriceControlsReadsTheMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldMarkTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeClearGoldTest;

	UCataclysmEmpireRun* Run = QuietClearRun(2741);
	if (!TestTrue(TEXT("set-up: a run with its cities"), Run->Map != nullptr && Run->Map->Cities.Num() > 4))
	{
		return false;
	}
	UCataclysmEmpireMap* Map = Run->Map;
	const int32 Bitten = Map->Cities[2].CityId;
	const int32 Untouched = Map->Cities[3].CityId;
	const int32 Lowered = Map->Cities[4].CityId;
	for (const int32 Index : { 2, 3, 4 })
	{
		Map->Cities[Index].Upgrades.AddDefaulted(1);
	}
	const bool bUnmarkedAtFirst = !Map->Find(Bitten)->bTookSurgeDamage && !Map->Find(Untouched)->bTookSurgeDamage
		&& !Map->Find(Lowered)->bTookSurgeDamage;

	Map->Damage(Bitten, 10.0f, 0.0f);
	Map->Damage(Untouched, 0.0f, 0.0f);
	FCataclysmCity* LoweredCity = Map->FindMutable(Lowered);
	LoweredCity->Defence = Map->EffectiveMaxDefence(*LoweredCity) - 50.0f;

	FCataclysmEmpireTreePlayerBonuses Controls;
	Controls.SurgeDamagedCityUpgradePriceReduced = 0.40f;
	const float OnBitten = UCataclysmEmpireTree::UpgradePriceFactorFor(Controls, Map, Bitten);
	const float OnUntouched = UCataclysmEmpireTree::UpgradePriceFactorFor(Controls, Map, Untouched);
	const float OnLowered = UCataclysmEmpireTree::UpgradePriceFactorFor(Controls, Map, Lowered);
	AddInfo(FString::Printf(TEXT("CLEAR GOLD MARK: bitten %s %.2f; hit for nothing %s %.2f; lowered with no bite %s %.2f"),
							Map->Find(Bitten)->bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"), OnBitten,
							Map->Find(Untouched)->bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"), OnUntouched,
							Map->Find(Lowered)->bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"), OnLowered));

	TestTrue(TEXT("set-up: a new run's cities are unmarked"), bUnmarkedAtFirst);
	TestTrue(TEXT("a bite that takes defence marks the city"), Map->Find(Bitten)->bTookSurgeDamage);
	TestFalse(TEXT("a hit for nothing does not"), Map->Find(Untouched)->bTookSurgeDamage);
	TestFalse(TEXT("nor does a city below its maximum with no bite"), Map->Find(Lowered)->bTookSurgeDamage);
	TestEqual(TEXT("Price Controls on the bitten city: 0.60"), OnBitten, 0.60f, 1.0e-4f);
	TestEqual(TEXT("and none on the city hit for nothing"), OnUntouched, 1.0f);
	TestEqual(TEXT("and none on the city below its maximum with no bite"), OnLowered, 1.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Stimulus Package (B2)
// ---------------------------------------------------------------------------

/**
 * The run notes its population below half and a reclaim ends it (B2): every standing city at a tenth of its maximum
 * population, a day's check sets the flag; the population back at full, the next day leaves it set; reclaiming a
 * fallen city clears it, and with the population still full the next day leaves it clear. A new run begins clear.
 * Prints "CLEAR GOLD STIMULUS FLAG".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldFlagTest,
	"Cataclysm.EmpireTreeClearGold.TheRunNotesPopulationBelowHalfAndAReclaimEndsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldFlagTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeClearGoldTest;

	UCataclysmEmpireRun* Run = QuietClearRun(2741);
	if (!TestTrue(TEXT("set-up: a run with its cities"), Run->Map != nullptr && Run->Map->Cities.Num() > 2))
	{
		return false;
	}
	const bool bClearAtFirst = !Run->bPopulationFellBelowHalf;

	for (FCataclysmCity& City : Run->Map->Cities)
	{
		City.Population = 0.1f * Run->Map->EffectiveMaxPopulation(City);
	}
	Run->AdvanceDay();
	const bool bSetBelowHalf = Run->bPopulationFellBelowHalf;

	for (FCataclysmCity& City : Run->Map->Cities)
	{
		City.Population = Run->Map->EffectiveMaxPopulation(City);
	}
	Run->AdvanceDay();
	const bool bKeptAtFull = Run->bPopulationFellBelowHalf;

	FCataclysmCity& Lost = Run->Map->Cities[1];
	const int32 LostId = Lost.CityId;
	Run->Map->Fall(LostId);
	PlaceOn(*Run, 9600, ECataclysmDungeonType::FallenCity, *Run->Map->Find(LostId));
	Run->ClearDungeon(9600);
	const bool bReclaimed = !Run->Map->Find(LostId)->bFallen;
	const bool bClearedByTheReclaim = !Run->bPopulationFellBelowHalf;
	for (FCataclysmCity& City : Run->Map->Cities)
	{
		City.Population = Run->Map->EffectiveMaxPopulation(City);
	}
	Run->NotePopulationAgainstHalf();
	const bool bStillClear = !Run->bPopulationFellBelowHalf;
	AddInfo(FString::Printf(TEXT("CLEAR GOLD STIMULUS FLAG: at first %s; below half %s; back at full %s; reclaimed %s, "
								 "then %s; full again %s"),
							bClearAtFirst ? TEXT("clear") : TEXT("SET"), bSetBelowHalf ? TEXT("set") : TEXT("CLEAR"),
							bKeptAtFull ? TEXT("still set") : TEXT("CLEAR"), bReclaimed ? TEXT("yes") : TEXT("NO"),
							bClearedByTheReclaim ? TEXT("clear") : TEXT("SET"), bStillClear ? TEXT("clear") : TEXT("SET")));

	TestTrue(TEXT("a new run begins clear"), bClearAtFirst);
	TestTrue(TEXT("a day with every city at a tenth sets it"), bSetBelowHalf);
	TestTrue(TEXT("the population back at full does not clear it"), bKeptAtFull);
	TestTrue(TEXT("set-up: the fallen city was reclaimed"), bReclaimed);
	TestTrue(TEXT("the reclaim clears it"), bClearedByTheReclaim);
	TestTrue(TEXT("and at full the next check leaves it clear"), bStillClear);
	return true;
}

/**
 * Stimulus Package doubles a kill's gold only while the run's flag is set (B2): with the node, the multiplier is 2 with
 * the flag and 1 without; without the node, 1 either way; and the payout at a clear is the same with the flag as
 * without, because it is a kill's alone. Prints "CLEAR GOLD STIMULUS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldStimulusTest,
	"Cataclysm.EmpireTreeClearGold.StimulusDoublesKillGoldOnlyWhileTheRunIsBelowHalf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldStimulusTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeClearGoldTest;

	FCataclysmEmpireTreePlayerBonuses Stimulus;
	Stimulus.GoldDoubledBelowHalfPopulation = 1.0f;
	Stimulus.GoldPerFloorAtClear = 50.0f;
	FCataclysmEmpireTreeDungeonState Below;
	Below.bPopulationBelowHalf = true;
	const FCataclysmEmpireTreeDungeonState Above;

	const float WithFlag = UCataclysmEmpireTree::GoldMultiplierFor(Stimulus, Below);
	const float WithoutFlag = UCataclysmEmpireTree::GoldMultiplierFor(Stimulus, Above);
	const float NoNode = UCataclysmEmpireTree::GoldMultiplierFor(FCataclysmEmpireTreePlayerBonuses(), Below);
	const int64 PayBelow = UCataclysmEmpireTree::ClearPayFor(Stimulus, Below, 10, 1.0, 0, 0).Salvage;
	const int64 PayAbove = UCataclysmEmpireTree::ClearPayFor(Stimulus, Above, 10, 1.0, 0, 0).Salvage;
	AddInfo(FString::Printf(TEXT("CLEAR GOLD STIMULUS: with the flag %.2f, without %.2f, no node %.2f; the clear pays %lld "
								 "with the flag and %lld without"),
							WithFlag, WithoutFlag, NoNode, PayBelow, PayAbove));

	TestEqual(TEXT("the node and the flag: doubled"), WithFlag, 2.0f, 1.0e-4f);
	TestEqual(TEXT("the node without the flag: 1"), WithoutFlag, 1.0f);
	TestEqual(TEXT("the flag without the node: 1"), NoNode, 1.0f);
	TestEqual(TEXT("the payout at a clear is not doubled: 500 either way"), PayBelow, PayAbove);
	TestEqual(TEXT("and it is 500"), PayAbove, static_cast<int64>(500));
	return true;
}

// ---------------------------------------------------------------------------
// The payout at a clear (B3 to B5)
// ---------------------------------------------------------------------------

/**
 * The payout at a clear (B3 to B5): Salvage Rights at 1000 a floor, 12 floors, tier 2's scale of 3, raised by Investment
 * Funds' 1.00 and Wartime Economy's 0.10 for each of 3 active types, is 1000 x 12 x 3 x 2.30 = 82,800; Coinage's 0.74
 * and The Midas Touch's 0.50 do not reach it. Cleanup Crew's 0.10 of 1234 dropped less 1000 picked up is 23, rounded
 * down; with more picked up than dropped, nothing. No node pays nothing. Prints "CLEAR GOLD PAY".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldPayTest,
	"Cataclysm.EmpireTreeClearGold.TheClearPaysSalvageByFloorsAndScaleAndATenthOfTheGoldLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldPayTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeClearGoldTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.GoldPerFloorAtClear = 1000.0f;
	Bonuses.ClearPayoutIncreased = 1.0f;
	Bonuses.GoldPerActiveType = 0.10f;
	Bonuses.GoldIncreased = 0.74f;
	Bonuses.GoldMore = 0.50f;
	Bonuses.UnpickedGoldShareAtClear = 0.10f;
	FCataclysmEmpireTreeDungeonState State;
	State.ActiveTypes = 3;

	const FCataclysmEmpireTreeClearPay Pay = UCataclysmEmpireTree::ClearPayFor(Bonuses, State, 12, 3.0, 1234, 1000);
	const FCataclysmEmpireTreeClearPay MorePickedUp = UCataclysmEmpireTree::ClearPayFor(Bonuses, State, 12, 3.0, 900, 1000);
	const FCataclysmEmpireTreeClearPay NoNode =
		UCataclysmEmpireTree::ClearPayFor(FCataclysmEmpireTreePlayerBonuses(), State, 12, 3.0, 1234, 1000);
	AddInfo(FString::Printf(TEXT("CLEAR GOLD PAY: salvage %lld, cleanup %lld, total %lld; more picked up than dropped %lld; "
								 "no node %lld"),
							Pay.Salvage, Pay.Cleanup, Pay.Total(), MorePickedUp.Cleanup, NoNode.Total()));

	TestEqual(TEXT("Salvage Rights: 82,800"), Pay.Salvage, static_cast<int64>(82800));
	TestEqual(TEXT("Cleanup Crew: 23"), Pay.Cleanup, static_cast<int64>(23));
	TestEqual(TEXT("together: 82,823"), Pay.Total(), static_cast<int64>(82823));
	TestEqual(TEXT("more picked up than dropped: nothing"), MorePickedUp.Cleanup, static_cast<int64>(0));
	TestEqual(TEXT("no node: nothing"), NoNode.Total(), static_cast<int64>(0));
	return true;
}

/**
 * The gold scale is the tier's pile over tier 1's (R10): 1 at tier 1, 3 at tier 2, 30 at tier 8. Prints "CLEAR GOLD
 * SCALE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldScaleTest,
	"Cataclysm.EmpireTreeClearGold.TheGoldScaleIsTheTiersPileOverTierOnes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldScaleTest::RunTest(const FString&)
{
	const double One = UCataclysmDropRoll::GoldScaleFor(1);
	const double Two = UCataclysmDropRoll::GoldScaleFor(2);
	const double Eight = UCataclysmDropRoll::GoldScaleFor(8);
	AddInfo(FString::Printf(TEXT("CLEAR GOLD SCALE: tier 1 %.2f, tier 2 %.2f, tier 8 %.2f"), One, Two, Eight));

	TestEqual(TEXT("tier 1: 1"), One, 1.0, 1.0e-9);
	TestEqual(TEXT("tier 2: 3"), Two, 3.0, 1.0e-9);
	TestEqual(TEXT("tier 8: 30"), Eight, 30.0, 1.0e-9);
	return true;
}

/**
 * The walk's gold goes into the walk record and back: a game mode that counted 1234 dropped and 1000 picked up writes
 * them into the walk it saves, and another reads them back. Prints "CLEAR GOLD WALK".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldWalkTest,
	"Cataclysm.EmpireTreeClearGold.TheWalksGoldIsSavedAndRestoredWithTheWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldWalkTest::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: the dungeon game mode"), Mode))
	{
		return false;
	}
	ACataclysmDungeonGameMode::NoteGoldDroppedIn(World, 1234);
	ACataclysmDungeonGameMode::NoteGoldPickedUpIn(World, 1000);
	const FCataclysmSavedWalk Walk = Mode->WalkStateNow();
	Mode->GoldDroppedThisWalk = 0;
	Mode->GoldPickedUpThisWalk = 0;
	Mode->RestoreWalkState(Walk);
	AddInfo(FString::Printf(TEXT("CLEAR GOLD WALK: saved %lld dropped and %lld picked up; restored %lld and %lld"),
							Walk.GoldDroppedThisWalk, Walk.GoldPickedUpThisWalk, Mode->GoldDroppedThisWalk,
							Mode->GoldPickedUpThisWalk));

	TestEqual(TEXT("the walk saves 1234 dropped"), Walk.GoldDroppedThisWalk, static_cast<int64>(1234));
	TestEqual(TEXT("and 1000 picked up"), Walk.GoldPickedUpThisWalk, static_cast<int64>(1000));
	TestEqual(TEXT("and restores 1234"), Mode->GoldDroppedThisWalk, static_cast<int64>(1234));
	TestEqual(TEXT("and 1000"), Mode->GoldPickedUpThisWalk, static_cast<int64>(1000));
	return true;
}

// ---------------------------------------------------------------------------
// The run record, version 12
// ---------------------------------------------------------------------------

/**
 * A saved run keeps its marks and its flag: city 2 marked and the flag set, written out by the save and read back into
 * a fresh run. Prints "CLEAR GOLD SAVED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldSavedTest,
	"Cataclysm.EmpireTreeClearGold.ASavedRunKeepsItsMarksAndItsFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldSavedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeClearGoldTest;

	UCataclysmEmpireRun* Run = QuietClearRun(2741);
	if (!TestTrue(TEXT("set-up: a run with its cities"), Run->Map != nullptr && Run->Map->Cities.Num() > 3))
	{
		return false;
	}
	const int32 Marked = Run->Map->Cities[2].CityId;
	const int32 Unmarked = Run->Map->Cities[3].CityId;
	Run->Map->FindMutable(Marked)->bTookSurgeDamage = true;
	Run->bPopulationFellBelowHalf = true;

	UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
	Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
	FString Why;
	FCataclysmSaveGather::RunClockFrom(*Run, *Record);
	FCataclysmSaveGather::CitiesFrom(*Run, *Record);
	FCataclysmSaveGather::SurgeScheduleFrom(*Run, *Record);
	FCataclysmSaveGather::ProgressFrom(*Run, *Record);
	FCataclysmSaveGather::IdentityFrom(*Run, *Record);
	FString Json;
	const bool bWritten = FCataclysmSaveGather::DungeonsFrom(*Run, *Record, Why)
		&& FCataclysmSaveStorage::ToJson(Record, Json, Why);
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	UCataclysmRunSave* Read = bWritten ? Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why)) : nullptr;
	if (!TestNotNull(TEXT("set-up: the record was written and read back"), Read))
	{
		AddError(Why);
		return false;
	}
	UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
	const bool bLoaded = FCataclysmSaveApply::RunInto(*Read, *Loaded, Why);
	const FCataclysmCity* LoadedMarked = Loaded->Map ? Loaded->Map->Find(Marked) : nullptr;
	const FCataclysmCity* LoadedUnmarked = Loaded->Map ? Loaded->Map->Find(Unmarked) : nullptr;
	AddInfo(FString::Printf(TEXT("CLEAR GOLD SAVED: loaded %s; city %d %s, city %d %s; the flag %s"),
							bLoaded ? TEXT("yes") : *Why, Marked,
							LoadedMarked && LoadedMarked->bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"), Unmarked,
							LoadedUnmarked && LoadedUnmarked->bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"),
							Loaded->bPopulationFellBelowHalf ? TEXT("set") : TEXT("clear")));

	TestTrue(TEXT("set-up: it loaded"), bLoaded);
	TestTrue(TEXT("the marked city comes back marked"), LoadedMarked != nullptr && LoadedMarked->bTookSurgeDamage);
	TestTrue(TEXT("the other comes back unmarked"), LoadedUnmarked != nullptr && !LoadedUnmarked->bTookSurgeDamage);
	TestTrue(TEXT("and the flag comes back set"), Loaded->bPopulationFellBelowHalf);
	return true;
}

/**
 * `Run_v11.json`, written before version 12, migrates unmarked and uncounted: false on both its cities, false on the
 * run, 0 and 0 on the walk, and every other field kept. CONTROL: the committed run file, `Run_v18.json` since issue #2848 (`Run_v17.json` from issue #2840, `Run_v16.json` from issue #2838, `Run_v15.json` from issue #2741 slice 9), reads its
 * first city marked, its second not, the flag set and the walk's 1234 and 1000. Prints "CLEAR GOLD MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeClearGoldMigrationTest,
	"Cataclysm.EmpireTreeClearGold.ARunFileFromVersion11MigratesUnmarkedAndUncounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeClearGoldMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeClearGoldTest;

	FString Text;
	FString CurrentText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v11.json"), Text, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Run_v18.json"), CurrentText, Reason))
	{
		AddError(Reason);
		return false;
	}
	const UCataclysmRunSave* Migrated = ReadRunText(*this, Text, TEXT("Run_v11.json"));
	const UCataclysmRunSave* Current = ReadRunText(*this, CurrentText, TEXT("Run_v18.json"));
	if (!Migrated || !Current || !TestEqual(TEXT("set-up: Run_v11.json has two cities"), Migrated->Cities.Num(), 2)
		|| !TestEqual(TEXT("set-up: Run_v18.json has two cities"), Current->Cities.Num(), 2))
	{
		return false;
	}

	// EVERY OTHER FIELD KEPT: the version 11 file at the current version with what the step writes, compared with what
	// the record writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion12Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion13Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion14Defaults(*Expected); // And 14, #46 G3 and G5: socket lists, no gem on the floor.
	CataclysmSaveFixtures::AddTheRunsVersion15Defaults(*Expected); // And issue #2741 slice 9's step.
	CataclysmSaveFixtures::AddTheRunsVersion16Defaults(*Expected); // And issue #2838's step.
	CataclysmSaveFixtures::AddTheRunsVersion17Defaults(*Expected); // And issue #2840's step.
	CataclysmSaveFixtures::AddTheRunsVersion18Defaults(*Expected); // And issue #2848's step: where each item arrived.
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Migrated, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);
	AddInfo(FString::Printf(TEXT("CLEAR GOLD MIGRATED: version %d; cities %s %s; flag %s; walk %lld and %lld; the rest %s; "
								 "Run_v18.json cities %s %s, flag %s, walk %lld and %lld"),
							Migrated->SchemaVersion, Migrated->Cities[0].bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"),
							Migrated->Cities[1].bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"),
							Migrated->bPopulationFellBelowHalf ? TEXT("set") : TEXT("clear"),
							Migrated->Walk.GoldDroppedThisWalk, Migrated->Walk.GoldPickedUpThisWalk,
							bSame ? TEXT("kept") : *Where,
							Current->Cities[0].bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"),
							Current->Cities[1].bTookSurgeDamage ? TEXT("marked") : TEXT("unmarked"),
							Current->bPopulationFellBelowHalf ? TEXT("set") : TEXT("clear"),
							Current->Walk.GoldDroppedThisWalk, Current->Walk.GoldPickedUpThisWalk));

	TestEqual(TEXT("it arrives at the current version"), Migrated->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestFalse(TEXT("its first city unmarked"), Migrated->Cities[0].bTookSurgeDamage);
	TestFalse(TEXT("and its second"), Migrated->Cities[1].bTookSurgeDamage);
	TestFalse(TEXT("the run's flag clear"), Migrated->bPopulationFellBelowHalf);
	TestEqual(TEXT("no gold dropped in the walk"), Migrated->Walk.GoldDroppedThisWalk, static_cast<int64>(0));
	TestEqual(TEXT("none picked up"), Migrated->Walk.GoldPickedUpThisWalk, static_cast<int64>(0));
	TestTrue(TEXT("and every other field is kept"), bSame);
	TestTrue(TEXT("CONTROL: Run_v18.json's first city is marked and its second is not"),
			 Current->Cities[0].bTookSurgeDamage && !Current->Cities[1].bTookSurgeDamage);
	TestTrue(TEXT("CONTROL: its flag is set"), Current->bPopulationFellBelowHalf);
	TestTrue(TEXT("CONTROL: its walk holds 1234 and 1000"),
			 Current->Walk.GoldDroppedThisWalk == 1234 && Current->Walk.GoldPickedUpThisWalk == 1000);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
