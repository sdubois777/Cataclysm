// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmCityUpgradeMapping.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "EngineUtils.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The five city upgrades that change what a kill pays in a dungeon standing on the city. Issue #2781, the coordinating
 * session's rulings 1 to 4 and readings X1 to X6 of 2026-11-20:
 *
 *   Dungeons here provide 2x more experience        the creature's figure doubled; skill experience follows (1, X4)
 *   Dungeons here have 10% increased magic find      ten points added to the kill's magic find (2, X3)
 *   Dungeons here drop 25% more gold                 each pile, times the empire tree's gold multiplier (X5)
 *   Dungeons here drop 25% more loot                 the kill's loot quantity, so its gear and materials (X2)
 *   Dungeons here drop 25% more crafting materials   the kill's ordinary material roll alone (X2)
 *
 * Each at its tier 1 figure (ruling 3), for kills in a dungeon standing on the city that holds it (ruling 4), and none
 * for a FALLEN city, whose upgrades pay again once it is reclaimed (X1: the design document says nothing of a fallen
 * city's upgrades, and Fiscal Policy and The Cartographer's Blessing count standing cities only).
 * `Cataclysm.CityDungeonRewards.` IS A NEW GROUP and selects these and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1: every figure below is reasoned from the code, and the run that proves them is
 * registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmCityDungeonRewardsTest
{
	const TCHAR* const Experience = TEXT("Treasurer_Dungeons_here_provide_2x_more_experience");
	const TCHAR* const MagicFind = TEXT("Treasurer_Dungeons_here_have_10_increased_magic_f");
	const TCHAR* const Gold = TEXT("Treasurer_Dungeons_here_drop_25_more_gold");
	const TCHAR* const Loot = TEXT("Treasurer_Dungeons_here_drop_25_more_loot");
	const TCHAR* const Materials = TEXT("Artisan_Dungeons_here_drop_25_more_crafting_mat");
	const TCHAR* const Architect = TEXT("Architect_Increase_max_defense_by_20");

	/** The five, as the table makes them. */
	TArray<FCataclysmCityUpgrade> TheFive()
	{
		TArray<FCataclysmCityUpgrade> Out;
		for (const TCHAR* Row : { Experience, MagicFind, Gold, Loot, Materials })
		{
			Out.Add(UCataclysmCityUpgradeMapping::MakeFromTable(FName(Row)));
		}
		return Out;
	}

	/** Whether the rewards are exactly the five's figures. */
	bool AreTheFive(const FCataclysmCityDungeonRewards& Rewards)
	{
		return FMath::IsNearlyEqual(Rewards.ExperienceMore, 2.0f, 1.0e-4f)
			&& FMath::IsNearlyEqual(Rewards.MagicFindIncreased, 10.0f, 1.0e-3f)
			&& FMath::IsNearlyEqual(Rewards.GoldMore, 1.25f, 1.0e-4f)
			&& FMath::IsNearlyEqual(Rewards.LootMore, 1.25f, 1.0e-4f)
			&& FMath::IsNearlyEqual(Rewards.MaterialsMore, 1.25f, 1.0e-4f);
	}

	FString Describe(const FCataclysmCityDungeonRewards& Rewards)
	{
		return FString::Printf(TEXT("experience x%.2f, magic find +%.1f, gold x%.2f, loot x%.2f, materials x%.2f"),
							   Rewards.ExperienceMore, Rewards.MagicFindIncreased, Rewards.GoldMore, Rewards.LootMore,
							   Rewards.MaterialsMore);
	}

	/** A run begun on this seed, its first day spent and its board emptied, as the empire tree tests leave one. */
	UCataclysmEmpireRun* QuietRewardsRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** A Basic dungeon on this city, with its timer, entered by the game mode; false after saying why. */
	bool EnterADungeonOn(FAutomationTestBase& Test, ACataclysmDungeonGameMode& Mode, UCataclysmEmpireRun& Run,
						 const FCataclysmCity& City)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 10;
		Dungeon.ResolveDays = 40.0f;
		Dungeon.SpawnedDay = Run.Day();
		Run.NextDungeonId = Dungeon.DungeonId + 1;
		Run.Dungeons.Add(Dungeon);
		return Test.TestTrue(TEXT("set-up: its timer"), Run.Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors)
								 && Run.Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays))
			&& Test.TestTrue(TEXT("set-up: the dungeon was entered"), Mode.EnterEmpireDungeon(Dungeon.DungeonId));
	}

	/** Gear and crafting materials lying in the world. */
	void CountTheFloor(UWorld* World, int32& OutGear, int32& OutMaterials)
	{
		OutGear = 0;
		OutMaterials = 0;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (!IsValid(*It))
			{
				continue;
			}
			if (It->IsMaterial())
			{
				++OutMaterials;
			}
			else if (!It->Item.Base.IsNone())
			{
				++OutGear;
			}
		}
	}

	void ClearTheFloor(UWorld* World)
	{
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			It->Destroy();
		}
	}

	constexpr int32 CataclysmBossStep = 5;
}

/**
 * Each upgrade gives its tier 1 figure and nothing else: the five together give experience x2.00, magic find +10,
 * gold, loot and materials x1.25; the gold one alone gives only its x1.25; an Architect upgrade and an empty list give
 * nothing; and the experience figure 137 is 274 at x2 and 137 with nothing. Prints "REWARDS EACH".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityDungeonRewardsEachTest,
	"Cataclysm.CityDungeonRewards.EachUpgradeGivesItsTierOneFigure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityDungeonRewardsEachTest::RunTest(const FString&)
{
	using namespace CataclysmCityDungeonRewardsTest;

	const TArray<FCataclysmCityUpgrade> Five = TheFive();
	if (!TestEqual(TEXT("set-up: the table makes all five"),
				   Five.FilterByPredicate([](const FCataclysmCityUpgrade& One) { return One.IsValid(); }).Num(), 5))
	{
		return false;
	}

	const FCataclysmCityDungeonRewards All = UCataclysmCityUpgradeRules::DungeonRewardsOf(Five);
	const FCataclysmCityDungeonRewards GoldAlone = UCataclysmCityUpgradeRules::DungeonRewardsOf(
		{ UCataclysmCityUpgradeMapping::MakeFromTable(FName(Gold)) });
	const FCataclysmCityDungeonRewards ArchitectAlone = UCataclysmCityUpgradeRules::DungeonRewardsOf(
		{ UCataclysmCityUpgradeMapping::MakeFromTable(FName(Architect)) });
	const FCataclysmCityDungeonRewards Empty = UCataclysmCityUpgradeRules::DungeonRewardsOf({});
	AddInfo(FString::Printf(TEXT("REWARDS EACH: the five give %s; the gold one alone %s; the Architect one %s; "
								 "experience 137 becomes %d, and %d with nothing"),
							*Describe(All), *Describe(GoldAlone), *Describe(ArchitectAlone), All.ExperienceFrom(137),
							Empty.ExperienceFrom(137)));

	TestTrue(TEXT("the five: x2.00, +10, x1.25, x1.25, x1.25"), AreTheFive(All));
	TestEqual(TEXT("the gold one alone: gold x1.25"), GoldAlone.GoldMore, 1.25f, 1.0e-4f);
	TestEqual(TEXT("and no more experience"), GoldAlone.ExperienceMore, 1.0f);
	TestEqual(TEXT("and no more loot"), GoldAlone.LootMore, 1.0f);
	TestFalse(TEXT("an Architect upgrade gives a kill nothing"), ArchitectAlone.GivesAnything());
	TestFalse(TEXT("and an empty list nothing"), Empty.GivesAnything());
	TestEqual(TEXT("experience 137 at x2: 274"), All.ExperienceFrom(137), 274);
	TestEqual(TEXT("and 137 with nothing"), Empty.ExperienceFrom(137), 137);
	return true;
}

/**
 * The bound dungeon's city gives them and another city does not (ruling 4): with city 0 holding the five, a dungeon on
 * city 0 gives them, here and through the world as a kill asks; a dungeon on city 1, which holds none, gives nothing;
 * and with no dungeon bound nothing either. Prints "REWARDS BOUND".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityDungeonRewardsBoundTest,
	"Cataclysm.CityDungeonRewards.TheBoundDungeonsCityGivesThemAndAnotherCityDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityDungeonRewardsBoundTest::RunTest(const FString&)
{
	using namespace CataclysmCityDungeonRewardsTest;

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
	Mode->StartPlay();
	UCataclysmEmpireRun* Run = QuietRewardsRun(2781);
	Mode->SetEmpireRunForTests(Run);
	Run->Map->Cities[0].Upgrades = TheFive();
	Run->Map->Cities[1].Upgrades.Empty();

	const FCataclysmCityDungeonRewards Unbound = Mode->CityRewardsHere();
	if (!EnterADungeonOn(*this, *Mode, *Run, Run->Map->Cities[0]))
	{
		return false;
	}
	const FCataclysmCityDungeonRewards OnTheCity = Mode->CityRewardsHere();
	const FCataclysmCityDungeonRewards Through = ACataclysmDungeonGameMode::CityRewardsIn(World);
	Mode->LeaveEmpireDungeon();
	if (!EnterADungeonOn(*this, *Mode, *Run, Run->Map->Cities[1]))
	{
		return false;
	}
	const FCataclysmCityDungeonRewards Elsewhere = Mode->CityRewardsHere();
	Mode->LeaveEmpireDungeon();
	AddInfo(FString::Printf(TEXT("REWARDS BOUND: none bound %s; on city 0 %s; through the world %s; on city 1 %s"),
							*Describe(Unbound), *Describe(OnTheCity), *Describe(Through), *Describe(Elsewhere)));

	TestFalse(TEXT("no dungeon bound: nothing"), Unbound.GivesAnything());
	TestTrue(TEXT("a dungeon on the city holding them: the five"), AreTheFive(OnTheCity));
	TestTrue(TEXT("and through the world, as a kill asks"), AreTheFive(Through));
	TestFalse(TEXT("a dungeon on a city holding none: nothing"), Elsewhere.GivesAnything());
	return true;
}

/**
 * THE CONTROL FOR X1: a dungeon on a FALLEN city whose upgrades include all five gets none of them, though the city
 * keeps them; and once the city is reclaimed the same dungeon gets all five again. Prints "REWARDS FALLEN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityDungeonRewardsFallenTest,
	"Cataclysm.CityDungeonRewards.AFallenCitysUpgradesGiveNothingUntilItIsReclaimed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityDungeonRewardsFallenTest::RunTest(const FString&)
{
	using namespace CataclysmCityDungeonRewardsTest;

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
	Mode->StartPlay();
	UCataclysmEmpireRun* Run = QuietRewardsRun(2781);
	Mode->SetEmpireRunForTests(Run);
	FCataclysmCity& City = Run->Map->Cities[0];
	City.Upgrades = TheFive();
	if (!EnterADungeonOn(*this, *Mode, *Run, City))
	{
		return false;
	}

	City.bFallen = true;
	const FCataclysmCityDungeonRewards Fallen = Mode->CityRewardsHere();
	const int32 KeptWhileFallen = City.Upgrades.Num();
	City.bFallen = false;
	const FCataclysmCityDungeonRewards Reclaimed = Mode->CityRewardsHere();
	Mode->LeaveEmpireDungeon();
	AddInfo(FString::Printf(TEXT("REWARDS FALLEN: fallen, holding %d upgrades, %s; reclaimed %s"), KeptWhileFallen,
							*Describe(Fallen), *Describe(Reclaimed)));

	TestEqual(TEXT("set-up: the fallen city still holds all five"), KeptWhileFallen, 5);
	TestFalse(TEXT("a dungeon on the fallen city: none of them"), Fallen.GivesAnything());
	TestTrue(TEXT("and once it is reclaimed: all five again"), AreTheFive(Reclaimed));
	return true;
}

/**
 * The materials multiplier is the material roll's alone, and at exactly 1 it rolls what was rolled before: from one
 * seed, a Cataclysm Boss's drops named with no multiplier and at 1 leave the same gear, the same materials and the
 * stream at the same place; at 20 the gear is the same and the materials are more. Prints "REWARDS MATERIALS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityDungeonRewardsMaterialsTest,
	"Cataclysm.CityDungeonRewards.MoreMaterialsIsTheMaterialRollsAloneAndOneRollsAsBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityDungeonRewardsMaterialsTest::RunTest(const FString&)
{
	using namespace CataclysmCityDungeonRewardsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float LootQuantity = UCataclysmDropRoll::BaselineLootQuantity;
	FRandomStream NamedNone(2781);
	UCataclysmDropSpawner::SpawnDropsFor(World, CataclysmBossStep, 0.0f, LootQuantity, FVector::ZeroVector, NamedNone);
	int32 GearNone = 0;
	int32 MaterialsNone = 0;
	CountTheFloor(World, GearNone, MaterialsNone);
	ClearTheFloor(World);

	FRandomStream AtOne(2781);
	UCataclysmDropSpawner::SpawnDropsFor(World, CataclysmBossStep, 0.0f, LootQuantity, FVector::ZeroVector, AtOne,
										 false, false, -1, -1, 0.0f, ECataclysmRarity::Everyday, 1.0f);
	int32 GearOne = 0;
	int32 MaterialsOne = 0;
	CountTheFloor(World, GearOne, MaterialsOne);
	ClearTheFloor(World);

	FRandomStream AtTwenty(2781);
	UCataclysmDropSpawner::SpawnDropsFor(World, CataclysmBossStep, 0.0f, LootQuantity, FVector::ZeroVector, AtTwenty,
										 false, false, -1, -1, 0.0f, ECataclysmRarity::Everyday, 20.0f);
	int32 GearTwenty = 0;
	int32 MaterialsTwenty = 0;
	CountTheFloor(World, GearTwenty, MaterialsTwenty);
	AddInfo(FString::Printf(TEXT("REWARDS MATERIALS: named none %d gear and %d materials; at 1 %d and %d; at 20 %d and "
								 "%d; the streams at none and 1 end at %d and %d"),
							GearNone, MaterialsNone, GearOne, MaterialsOne, GearTwenty, MaterialsTwenty,
							NamedNone.GetCurrentSeed(), AtOne.GetCurrentSeed()));

	TestTrue(TEXT("set-up: the kill dropped something"), GearNone + MaterialsNone > 0);
	TestEqual(TEXT("at 1: the same gear"), GearOne, GearNone);
	TestEqual(TEXT("the same materials"), MaterialsOne, MaterialsNone);
	TestEqual(TEXT("and the stream at the same place"), AtOne.GetCurrentSeed(), NamedNone.GetCurrentSeed());
	TestEqual(TEXT("at 20: the gear is the gear rolled at 1"), GearTwenty, GearOne);
	TestTrue(TEXT("and the materials are more"), MaterialsTwenty > MaterialsOne);
	return true;
}

/**
 * The five can be bought on a city: with gold to spare, a city holding no upgrade is offered each of them as buyable,
 * with no refusal. Prints "REWARDS OFFERED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityDungeonRewardsOfferedTest,
	"Cataclysm.CityDungeonRewards.TheFiveCanBeBoughtOnACity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityDungeonRewardsOfferedTest::RunTest(const FString&)
{
	using namespace CataclysmCityDungeonRewardsTest;

	UCataclysmEmpireRun* Run = QuietRewardsRun(2781);
	if (!TestTrue(TEXT("set-up: a run with a map"), Run->Map != nullptr && Run->Map->Cities.Num() > 0))
	{
		return false;
	}
	Run->Map->Cities[0].Upgrades.Empty();
	const TArray<FCataclysmCityUpgradeOffer> Offers =
		UCataclysmCityScreenLayout::OffersFor(nullptr, Run, Run->Map->Cities[0].CityId, /*GoldHeld=*/100000000);

	int32 Buyable = 0;
	FString Lines;
	for (const TCHAR* Row : { Experience, MagicFind, Gold, Loot, Materials })
	{
		const FCataclysmCityUpgradeOffer* Offer = Offers.FindByPredicate(
			[Row](const FCataclysmCityUpgradeOffer& One) { return One.RowName == FName(Row); });
		const bool bCanBuy = Offer != nullptr && Offer->bCanBuy && Offer->Refusal.IsEmpty();
		Buyable += bCanBuy ? 1 : 0;
		Lines += FString::Printf(TEXT(" %s %s;"), Row, bCanBuy ? TEXT("buyable") : TEXT("REFUSED"));
	}
	AddInfo(FString::Printf(TEXT("REWARDS OFFERED:%s"), *Lines));

	TestEqual(TEXT("all five can be bought"), Buyable, 5);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
