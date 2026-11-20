// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmEmpireTree.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The empire upgrade tree's player nodes. Issue #2741 slice 5a: the fifteen the coordinating session's D1 to D4 of
 * 2026-11-19 put in this slice, each reading a judgement --
 *
 *   magic find      flat percentage points: Eyes of the Empire, The Hoarder, Quality over Quantity per floor removed,
 *                   Dungeon Expertise per distinct modifier on the floor, Manifest Wealth per floor cleared, Dungeon
 *                   Legend per whole five clears, The Cartographer's Blessing per Explorer city
 *   loot quantity   an increase: Bounty, The Hoarder, Spoils of War per active type, Speed Runner per whole two days
 *                   removed to 100%, Efficiency Premium per whole day to 50%, The Cartographer's Blessing, Temporal
 *                   Efficiency per whole five Artisan points to 50%
 *   the rest        Pathfinding's movement speed; Dungeon Mastery's attack and spell damage per kind defeated; One-Day
 *                   Specialist doubling the Explorer branch's loot when the walk is one day and was longer
 *
 * all of it put on the character by the dungeon game mode at every floor, beside the floor's own lines, and taken off
 * when the player leaves. `Cataclysm.EmpireTreeLoot.` IS A NEW GROUP and selects these and nothing else.
 */
namespace CataclysmEmpireTreeLootTest
{
	// The tree file's identifiers, by the node names the table gives them.
	const FName EyesOfTheEmpire(TEXT("basic_0000002a"));		// +5% magic find a point, 10 points
	const FName Bounty(TEXT("basic_0000002e"));				// +5% loot quantity a point, 15 points
	const FName Pathfinding(TEXT("basic_expl_t1g"));		// +2% movement speed a point, 10 points
	const FName SpoilsOfWar(TEXT("basic_expl_t3f"));		// +2% loot quantity a point per active type, 8 points
	const FName SpeedRunner(TEXT("basic_expl_t4d"));		// +5% a point per 2 full days removed, to 100%, 10 points
	const FName EfficiencyPremium(TEXT("keystone_0000002i"));	// +5% per day removed, to 50%
	const FName QualityOverQuantity(TEXT("keystone_0000002j"));	// +2% magic find per floor removed
	const FName DungeonExpertise(TEXT("basic_expl_t3d"));	// +4% magic find a point per modifier, 10 points
	const FName ManifestWealth(TEXT("basic_0000002s"));		// +1% magic find a point per floor cleared, 25 points
	const FName DungeonLegend(TEXT("basic_expl_t4e"));		// +1% magic find a point per 5 clears, 10 points
	const FName Cartographer(TEXT("capstone_0000002y"));	// +5% loot quantity and magic find per Explorer city
	const FName TemporalEfficiency(TEXT("keystone_0000002o"));	// +10% loot quantity per 5 Artisan points, to 50%
	const FName OneDaySpecialist(TEXT("keystone_0000002w"));	// Explorer loot doubled at a one-day walk
	const FName DungeonMastery(TEXT("basic_expl_t2f"));		// +3% damage a point per kind defeated, 10 points
	const FName Foundations(TEXT("capstone_0000000m"));		// option 3, The Hoarder: +25% magic find, +100% quantity
	const FName Scavenger(TEXT("basic_0000004z"));			// Artisan, 10 points: Temporal Efficiency's steps
	const FName ApprenticeSmith(TEXT("basic_0000004y"));	// Artisan, 10 points
	const FName MaterialConservation(TEXT("basic_00000050"));	// Artisan, 10 points
	const FName Pacing(TEXT("basic_00000029"));				// a slice 4 node, for the control

	const FName ExplorerUpgrade(TEXT("Explorer_Dungeons_here_have_5_more_floors"));
	const FName ArchitectUpgrade(TEXT("Architect_Increase_max_defense_by_20"));

	FCataclysmEmpireTreePlayerBonuses PlayerBonusesOf(const FCataclysmPassiveAllocation& Tree)
	{
		return UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
													  UCataclysmEmpireTree::LoadEffectTable(), Tree);
	}

	/** A run begun on this seed, its first day spent and its board emptied, as slice 4's tests leave one. */
	UCataclysmEmpireRun* QuietLootRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** A dungeon not on the map, for asking the state of. */
	FCataclysmDungeon LootDungeon(ECataclysmDungeonType Type, int32 Floors, float WalkDays, int32 UpgradeFloors = 0,
								  ECataclysmDungeonSubType SubType = ECataclysmDungeonSubType::None)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = 9900;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.CityId = INDEX_NONE;
		Dungeon.CityTier = ECataclysmCityTier::Outpost;
		Dungeon.Floors = Floors;
		Dungeon.WalkDays = WalkDays;
		Dungeon.UpgradeFloors = UpgradeFloors;
		Dungeon.Bosses = 1;
		return Dungeon;
	}

	/** A walk of these days from a default of these, every other count nought. */
	FCataclysmEmpireTreeDungeonState WalkOf(float DefaultWalkDays, float WalkDays)
	{
		FCataclysmEmpireTreeDungeonState State;
		State.DefaultWalkDays = DefaultWalkDays;
		State.WalkDays = WalkDays;
		return State;
	}

	/**
	 * A character built by hand with every attribute set and an equipment component, as
	 * `CataclysmDungeonModifierEffectsTests.cpp`'s is: a test world has no player to find. Named for this file so a
	 * unity build cannot meet another file's helper of the same name.
	 */
	struct FTreeLootCharacter
	{
		explicit FTreeLootCharacter(UWorld* InWorld)
		{
			Actor = InWorld->SpawnActor<AActor>();
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

		~FTreeLootCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Read(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

/**
 * The fifteen nodes add up through the generated tables at full points: Eyes of the Empire 50 magic find and Bounty
 * +75% in the Explorer branch, The Hoarder's 25 and +100% outside it; Temporal Efficiency's two steps of Scavenger's 10
 * Artisan points +20%; Pathfinding +20%; and each per-something figure its points' worth. And the other way: the city
 * bonuses of the same allocation are none, and a slice 4 node gives no player bonus.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootTableTest,
	"Cataclysm.EmpireTreeLoot.TheNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	if (!TestNotNull(TEXT("set-up: DT_EmpireTreeEffects"), UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(EyesOfTheEmpire, 10);
	Tree.Add(Bounty, 15);
	Tree.Add(Pathfinding, 10);
	Tree.Add(SpoilsOfWar, 8);
	Tree.Add(SpeedRunner, 10);
	Tree.Add(EfficiencyPremium, 1);
	Tree.Add(QualityOverQuantity, 1);
	Tree.Add(DungeonExpertise, 10);
	Tree.Add(ManifestWealth, 25);
	Tree.Add(DungeonLegend, 10);
	Tree.Add(Cartographer, 1);
	Tree.Add(TemporalEfficiency, 1);
	Tree.Add(OneDaySpecialist, 1);
	Tree.Add(DungeonMastery, 10);
	Tree.Add(Scavenger, 10);
	Tree.Add(Foundations, 1);
	Tree.SetChosenOption(Foundations, 3);
	const FCataclysmEmpireTreePlayerBonuses Held = PlayerBonusesOf(Tree);

	AddInfo(FString::Printf(TEXT("LOOT TABLE: MF %.2f + Explorer %.2f, LQ %.2f + Explorer %.2f, speed %.2f, per type %.2f, "
								 "per 2 days %.2f to %.2f, per day %.2f to %.2f, per floor removed %.2f, per modifier %.2f, "
								 "per floor cleared %.2f, per 5 clears %.2f, per city %.2f / %.2f, doubled at %d, damage %.2f"),
							Held.MagicFind, Held.ExplorerMagicFind, Held.LootQuantity, Held.ExplorerLootQuantity,
							Held.MovementSpeed, Held.LootQuantityPerActiveType, Held.LootQuantityPerTwoDaysRemoved,
							Held.TwoDaysRemovedCap, Held.LootQuantityPerDayRemoved, Held.DayRemovedCap,
							Held.MagicFindPerFloorRemoved, Held.MagicFindPerModifierActive, Held.MagicFindPerFloorCleared,
							Held.MagicFindPerFiveClears, Held.LootQuantityPerExplorerCity, Held.MagicFindPerExplorerCity,
							Held.ExplorerLootDoubledAtWalkDays, Held.DamagePerKindDefeated));
	TestEqual(TEXT("The Hoarder's magic find, outside the Explorer branch"), Held.MagicFind, 0.25f, 0.0001f);
	TestEqual(TEXT("Eyes of the Empire at 10 points, in it"), Held.ExplorerMagicFind, 0.50f, 0.0001f);
	TestEqual(TEXT("The Hoarder's loot quantity, outside it"), Held.LootQuantity, 1.00f, 0.0001f);
	TestEqual(TEXT("Bounty at 15 points and Temporal Efficiency at two Artisan steps, in it"),
			  Held.ExplorerLootQuantity, 0.75f + 0.20f, 0.0001f);
	TestEqual(TEXT("Pathfinding at 10 points"), Held.MovementSpeed, 0.20f, 0.0001f);
	TestEqual(TEXT("Spoils of War at 8 points"), Held.LootQuantityPerActiveType, 0.16f, 0.0001f);
	TestEqual(TEXT("Speed Runner at 10 points: 50% a step"), Held.LootQuantityPerTwoDaysRemoved, 0.50f, 0.0001f);
	TestEqual(TEXT("to 100%"), Held.TwoDaysRemovedCap, 1.00f, 0.0001f);
	TestEqual(TEXT("Efficiency Premium: 5% a day"), Held.LootQuantityPerDayRemoved, 0.05f, 0.0001f);
	TestEqual(TEXT("to 50%"), Held.DayRemovedCap, 0.50f, 0.0001f);
	TestEqual(TEXT("Quality over Quantity: 2% a floor"), Held.MagicFindPerFloorRemoved, 0.02f, 0.0001f);
	TestEqual(TEXT("Dungeon Expertise at 10 points: 40% a modifier"), Held.MagicFindPerModifierActive, 0.40f, 0.0001f);
	TestEqual(TEXT("Manifest Wealth at 25 points: 25% a floor"), Held.MagicFindPerFloorCleared, 0.25f, 0.0001f);
	TestEqual(TEXT("Dungeon Legend at 10 points: 10% a step"), Held.MagicFindPerFiveClears, 0.10f, 0.0001f);
	TestEqual(TEXT("The Cartographer's Blessing: 5% quantity a city"), Held.LootQuantityPerExplorerCity, 0.05f, 0.0001f);
	TestEqual(TEXT("and 5% magic find a city"), Held.MagicFindPerExplorerCity, 0.05f, 0.0001f);
	TestEqual(TEXT("One-Day Specialist at a walk of one day"), Held.ExplorerLootDoubledAtWalkDays, 1);
	TestEqual(TEXT("Dungeon Mastery at 10 points: 30% a kind"), Held.DamagePerKindDefeated, 0.30f, 0.0001f);

	// THE OTHER WAY: no slice 5a row reaches the cities, and no slice 4 row reaches the player.
	const FCataclysmEmpireTreeBonuses City = UCataclysmEmpireTree::CityBonusesFor(
		UCataclysmEmpireTree::LoadNodeTable(), UCataclysmEmpireTree::LoadEffectTable(), Tree);
	TestFalse(TEXT("the same allocation reaches nothing a city or a walk reads"), City.ReachesTheWalk());
	TestEqual(TEXT("and adds no floors"), City.FloorsAdded, 0);
	FCataclysmPassiveAllocation WalkOnly;
	WalkOnly.Add(Pacing, 10);
	const FCataclysmEmpireTreeDungeonState Anywhere = WalkOf(20.0f, 1.0f);
	TestEqual(TEXT("a slice 4 node puts no line on the player"),
			  UCataclysmEmpireTree::PlayerStatModifiersFor(PlayerBonusesOf(WalkOnly), Anywhere).Num(), 0);
	return true;
}

/**
 * Temporal Efficiency counts whole steps of five Artisan points (C3) at 10% each and stops at its +50% (D3): 9 points
 * give one step, 24 four, 30 six held at five.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootTemporalTest,
	"Cataclysm.EmpireTreeLoot.TemporalEfficiencyCountsWholeArtisanStepsToItsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootTemporalTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	auto Temporal = [](int32 ScavengerPoints, int32 SmithPoints, int32 ConservationPoints)
	{
		FCataclysmPassiveAllocation Tree;
		Tree.Add(TemporalEfficiency, 1);
		Tree.Add(Scavenger, ScavengerPoints);
		if (SmithPoints > 0)
		{
			Tree.Add(ApprenticeSmith, SmithPoints);
		}
		if (ConservationPoints > 0)
		{
			Tree.Add(MaterialConservation, ConservationPoints);
		}
		return PlayerBonusesOf(Tree).ExplorerLootQuantity;
	};

	TestEqual(TEXT("9 Artisan points: one step, +10%"), Temporal(9, 0, 0), 0.10f, 0.0001f);
	TestEqual(TEXT("24: four steps, +40%"), Temporal(10, 10, 4), 0.40f, 0.0001f);
	TestEqual(TEXT("30: six steps, held at +50%"), Temporal(10, 10, 10), 0.50f, 0.0001f);
	TestEqual(TEXT("4: no step"), Temporal(4, 0, 0), 0.0f, 0.0001f);
	return true;
}

// ---------------------------------------------------------------------------
// The arithmetic
// ---------------------------------------------------------------------------

/**
 * Magic find grows with what the dungeon and the run say: 2% a floor removed, 40% a modifier, 25% a floor cleared, 10%
 * a whole five clears (C3: 9 clears are one step, 10 two), 5% an Explorer city; in percentage points of flat magic find.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootMagicFindTest,
	"Cataclysm.EmpireTreeLoot.MagicFindGrowsWithTheDungeonTheFloorAndTheRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootMagicFindTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.MagicFindPerFloorRemoved = 0.02f;
	Bonuses.MagicFindPerModifierActive = 0.40f;
	Bonuses.MagicFindPerFloorCleared = 0.25f;
	Bonuses.MagicFindPerFiveClears = 0.10f;
	Bonuses.MagicFindPerExplorerCity = 0.05f;

	FCataclysmEmpireTreeDungeonState State = WalkOf(20.0f, 20.0f);
	State.FloorsRemoved = 5;
	State.ModifiersActive = 3;
	State.FloorsCleared = 4;
	State.DungeonsCleared = 9;
	State.ExplorerCities = 2;
	TestEqual(TEXT("10 + 120 + 100 + 10 + 10 magic find"), UCataclysmEmpireTree::MagicFindFor(Bonuses, State),
			  250.0f, 0.01f);

	State.DungeonsCleared = 10;
	TestEqual(TEXT("ten clears are two steps of Dungeon Legend"), UCataclysmEmpireTree::MagicFindFor(Bonuses, State),
			  260.0f, 0.01f);
	State.DungeonsCleared = 4;
	TestEqual(TEXT("four clears are none"), UCataclysmEmpireTree::MagicFindFor(Bonuses, State), 240.0f, 0.01f);

	TestEqual(TEXT("and an empty state gives nothing"),
			  UCataclysmEmpireTree::MagicFindFor(Bonuses, WalkOf(20.0f, 20.0f)), 0.0f, 0.0001f);
	return true;
}

/**
 * Loot quantity for days removed counts whole days and whole steps of two (C3), each node to its cap: 3.5 days removed
 * are 3, one step of Speed Runner and +15% of Efficiency Premium; 5 are two steps, held at 100%; 39 are both held.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootDaysTest,
	"Cataclysm.EmpireTreeLoot.LootQuantityCountsWholeDaysRemovedToEachCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootDaysTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.LootQuantityPerTwoDaysRemoved = 0.50f;
	Bonuses.TwoDaysRemovedCap = 1.00f;
	Bonuses.LootQuantityPerDayRemoved = 0.05f;
	Bonuses.DayRemovedCap = 0.50f;

	TestEqual(TEXT("3.5 days removed: 3 whole, +50% and +15%"),
			  UCataclysmEmpireTree::LootQuantityFor(Bonuses, WalkOf(20.0f, 16.5f)), 65.0f, 0.01f);
	TestEqual(TEXT("5 days: two steps held at +100%, and +25%"),
			  UCataclysmEmpireTree::LootQuantityFor(Bonuses, WalkOf(20.0f, 15.0f)), 125.0f, 0.01f);
	TestEqual(TEXT("39 days: both held, +100% and +50%"),
			  UCataclysmEmpireTree::LootQuantityFor(Bonuses, WalkOf(40.0f, 1.0f)), 150.0f, 0.01f);
	TestEqual(TEXT("no day removed: nothing"),
			  UCataclysmEmpireTree::LootQuantityFor(Bonuses, WalkOf(20.0f, 20.0f)), 0.0f, 0.0001f);

	// AND THE PER-TYPE AND PER-CITY FIGURES BESIDE THEM.
	FCataclysmEmpireTreePlayerBonuses Counted;
	Counted.LootQuantityPerActiveType = 0.16f;
	Counted.LootQuantityPerExplorerCity = 0.05f;
	FCataclysmEmpireTreeDungeonState State = WalkOf(20.0f, 20.0f);
	State.ActiveTypes = 4;
	State.ExplorerCities = 3;
	TestEqual(TEXT("4 types at 16% and 3 cities at 5%"), UCataclysmEmpireTree::LootQuantityFor(Counted, State),
			  79.0f, 0.01f);
	return true;
}

/**
 * One-Day Specialist doubles the Explorer branch's magic find and loot quantity, and not The Hoarder's, when the walk
 * is one day and was longer: 125 magic find and +250% at a one-day walk of a 20-day dungeon; 75 and +175% for a dungeon
 * that was one day anyway, for a walk of two days, and without the node.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootOneDayTest,
	"Cataclysm.EmpireTreeLoot.OneDaySpecialistDoublesOnlyTheExplorerLoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootOneDayTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.MagicFind = 0.25f;
	Bonuses.ExplorerMagicFind = 0.50f;
	Bonuses.LootQuantity = 1.00f;
	Bonuses.ExplorerLootQuantity = 0.75f;
	Bonuses.ExplorerLootDoubledAtWalkDays = 1;

	const FCataclysmEmpireTreeDungeonState OneDay = WalkOf(20.0f, 1.0f);
	TestTrue(TEXT("a one-day walk of a 20-day dungeon doubles"), UCataclysmEmpireTree::DoublesExplorerLoot(Bonuses, OneDay));
	TestEqual(TEXT("25 + 2 x 50 magic find"), UCataclysmEmpireTree::MagicFindFor(Bonuses, OneDay), 125.0f, 0.01f);
	TestEqual(TEXT("+100% + 2 x 75%"), UCataclysmEmpireTree::LootQuantityFor(Bonuses, OneDay), 250.0f, 0.01f);

	for (const FCataclysmEmpireTreeDungeonState& Plain : { WalkOf(1.0f, 1.0f), WalkOf(20.0f, 2.0f) })
	{
		TestFalse(TEXT("a dungeon one day anyway, or a two-day walk, does not"),
				  UCataclysmEmpireTree::DoublesExplorerLoot(Bonuses, Plain));
		TestEqual(TEXT("75 magic find"), UCataclysmEmpireTree::MagicFindFor(Bonuses, Plain), 75.0f, 0.01f);
		TestEqual(TEXT("+175%"), UCataclysmEmpireTree::LootQuantityFor(Bonuses, Plain), 175.0f, 0.01f);
	}

	Bonuses.ExplorerLootDoubledAtWalkDays = 0;
	TestFalse(TEXT("and without the node nothing doubles"), UCataclysmEmpireTree::DoublesExplorerLoot(Bonuses, OneDay));
	return true;
}

// ---------------------------------------------------------------------------
// What the run and the dungeon say
// ---------------------------------------------------------------------------

/**
 * The state of each dungeon kind, read from the run: a Basic dungeon's default depth is its floors less its upgrade
 * floors, so -5 of them are 5 removed and +5 none; a Fallen City's is the more of its bosses and its tier's least
 * floors; the boss dungeon's its base and the Basic clears. A Cow Level's default walk is two days a floor. Floors
 * cleared are the floor number less one; modifiers are counted once each; the kinds defeated are three counts and the
 * Fallen City clears left over, and the boss's when the run is won.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootStateTest,
	"Cataclysm.EmpireTreeLoot.TheDungeonsStateIsReadFromTheRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootStateTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	UCataclysmEmpireRun* Run = QuietLootRun(2741);
	const FName Starvation(TEXT("Starvation"));
	const FName Greed(TEXT("Greed"));

	const FCataclysmEmpireTreeDungeonState Shallower = UCataclysmEmpireTree::DungeonStateFor(
		*Run, LootDungeon(ECataclysmDungeonType::Basic, 15, 12.0f, -5), { Starvation, Greed, Starvation }, 4);
	TestEqual(TEXT("Basic, 5 floors fewer than its roll: 5 removed"), Shallower.FloorsRemoved, 5);
	TestEqual(TEXT("its default walk is its 15 floors"), Shallower.DefaultWalkDays, 15.0f, 0.0001f);
	TestEqual(TEXT("and its walk the 12 stored"), Shallower.WalkDays, 12.0f, 0.0001f);
	TestEqual(TEXT("two distinct modifiers, the repeat counted once"), Shallower.ModifiersActive, 2);
	TestEqual(TEXT("floor 4: three cleared"), Shallower.FloorsCleared, 3);
	TestEqual(TEXT("the run's active types"), Shallower.ActiveTypes, Run->ActiveCataclysms.Num());

	TestEqual(TEXT("Basic, 5 floors more: none removed"), UCataclysmEmpireTree::DungeonStateFor(
		*Run, LootDungeon(ECataclysmDungeonType::Basic, 25, 0.0f, 5), {}, 1).FloorsRemoved, 0);

	const int32 Least = UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::FallenCity,
														  ECataclysmCityTier::Outpost).LeastFloors;
	TestEqual(TEXT("Fallen City one floor below its tier's least: 1 removed"), UCataclysmEmpireTree::DungeonStateFor(
		*Run, LootDungeon(ECataclysmDungeonType::FallenCity, Least - 1, 0.0f), {}, 1).FloorsRemoved, 1);

	Run->CataclysmDungeonBaseFloors = 30;
	Run->BasicDungeonsCleared = 4;
	TestEqual(TEXT("the boss dungeon at 33 of its 34: 1 removed"), UCataclysmEmpireTree::DungeonStateFor(
		*Run, LootDungeon(ECataclysmDungeonType::Cataclysm, 33, 0.0f), {}, 1).FloorsRemoved, 1);

	const FCataclysmEmpireTreeDungeonState Cow = UCataclysmEmpireTree::DungeonStateFor(
		*Run, LootDungeon(ECataclysmDungeonType::Basic, 10, 20.0f, 0, ECataclysmDungeonSubType::CowLevel), {}, 1);
	TestEqual(TEXT("a Cow Level's default walk is two days a floor"), Cow.DefaultWalkDays, 20.0f, 0.0001f);
	TestEqual(TEXT("and none of it removed"), Cow.WalkDays, 20.0f, 0.0001f);

	// THE KINDS: 2 Basic, 1 Quest and 1 Fallen City left over of 4 clears; then the boss, which wins the run.
	Run->DungeonsCleared = 4;
	Run->BasicDungeonsCleared = 2;
	Run->QuestObjectives = 1;
	Run->bWon = false;
	const FCataclysmDungeon Any = LootDungeon(ECataclysmDungeonType::Basic, 10, 0.0f);
	TestEqual(TEXT("three kinds defeated"), UCataclysmEmpireTree::DungeonStateFor(*Run, Any, {}, 1).KindsDefeated, 3);
	TestEqual(TEXT("and the run's clears"), UCataclysmEmpireTree::DungeonStateFor(*Run, Any, {}, 1).DungeonsCleared, 4);
	Run->DungeonsCleared = 5;
	Run->bWon = true;
	TestEqual(TEXT("four, with the boss"), UCataclysmEmpireTree::DungeonStateFor(*Run, Any, {}, 1).KindsDefeated, 4);
	Run->DungeonsCleared = 3;
	Run->bWon = false;
	TestEqual(TEXT("two, with no Fallen City left over"),
			  UCataclysmEmpireTree::DungeonStateFor(*Run, Any, {}, 1).KindsDefeated, 2);
	return true;
}

/**
 * The Cartographer's Blessing counts standing cities holding at least one bought upgrade from the Explorer rows: a city
 * with an Explorer and an Architect upgrade counts once, one with two Explorer upgrades once, an Architect one not at
 * all, and a fallen city with an Explorer upgrade not at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootCitiesTest,
	"Cataclysm.EmpireTreeLoot.ExplorerCitiesAreStandingCitiesWithAnExplorerUpgrade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootCitiesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	UCataclysmEmpireRun* Run = QuietLootRun(2741);
	if (!TestTrue(TEXT("set-up: at least four cities"), Run->Map && Run->Map->Cities.Num() >= 4))
	{
		return false;
	}
	auto Bought = [](FName RowName)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = RowName;
		return Upgrade;
	};
	TArray<FCataclysmCity>& Cities = Run->Map->Cities;
	for (FCataclysmCity& City : Cities)
	{
		City.Upgrades.Reset();
		City.bFallen = false;
	}
	const FCataclysmDungeon Any = LootDungeon(ECataclysmDungeonType::Basic, 10, 0.0f);
	TestEqual(TEXT("no upgrade: no city"), UCataclysmEmpireTree::DungeonStateFor(*Run, Any, {}, 1).ExplorerCities, 0);

	Cities[0].Upgrades = { Bought(ExplorerUpgrade), Bought(ArchitectUpgrade) };
	Cities[1].Upgrades = { Bought(ArchitectUpgrade) };
	Cities[2].Upgrades = { Bought(ExplorerUpgrade) };
	Cities[2].bFallen = true;
	Cities[3].Upgrades = { Bought(ExplorerUpgrade), Bought(ExplorerUpgrade) };
	TestEqual(TEXT("the first and the fourth"), UCataclysmEmpireTree::DungeonStateFor(*Run, Any, {}, 1).ExplorerCities, 2);
	return true;
}

// ---------------------------------------------------------------------------
// The lines on the character
// ---------------------------------------------------------------------------

/**
 * The lines are the tree's own: flat magic find, increased loot quantity, movement speed, and attack and spell damage
 * from Dungeon Mastery for each kind, every one from `ECataclysmModifierSource::EmpireTree`, which may not grant More;
 * and a tree with no player node gives no line at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootLinesTest,
	"Cataclysm.EmpireTreeLoot.TheLinesAreFlatMagicFindAndIncreasesFromTheTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootLinesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.ExplorerMagicFind = 0.50f;
	Bonuses.ExplorerLootQuantity = 0.75f;
	Bonuses.MovementSpeed = 0.20f;
	Bonuses.DamagePerKindDefeated = 0.30f;
	FCataclysmEmpireTreeDungeonState State = WalkOf(20.0f, 20.0f);
	State.KindsDefeated = 2;

	const TMap<FName, TArray<FCataclysmStatModifier>> Lines = UCataclysmEmpireTree::PlayerStatModifiersFor(Bonuses, State);
	struct FWanted { const TCHAR* Stat; ECataclysmStatBucket Bucket; float Value; };
	const FWanted Wanted[] = {
		{ TEXT("magic_find"), ECataclysmStatBucket::Flat, 50.0f },
		{ TEXT("loot_quantity"), ECataclysmStatBucket::Increased, 75.0f },
		{ TEXT("movement_speed"), ECataclysmStatBucket::Increased, 20.0f },
		{ TEXT("attack_damage"), ECataclysmStatBucket::Increased, 60.0f },
		{ TEXT("spell_damage"), ECataclysmStatBucket::Increased, 60.0f },
	};
	TestEqual(TEXT("five stats carry a line"), Lines.Num(), 5);
	for (const FWanted& Each : Wanted)
	{
		const TArray<FCataclysmStatModifier>* Found = Lines.Find(FName(Each.Stat));
		if (!TestTrue(FString::Printf(TEXT("%s has one line"), Each.Stat), Found && Found->Num() == 1))
		{
			continue;
		}
		const FCataclysmStatModifier& Line = (*Found)[0];
		TestTrue(FString::Printf(TEXT("%s in its bucket"), Each.Stat), Line.Bucket == Each.Bucket);
		TestTrue(FString::Printf(TEXT("%s from the tree"), Each.Stat), Line.Source == ECataclysmModifierSource::EmpireTree);
		TestEqual(FString::Printf(TEXT("%s's figure"), Each.Stat), Line.Value, Each.Value, 0.01f);
	}
	TestFalse(TEXT("the tree may not grant More"), UCataclysmStatPipeline::CanGrantMore(ECataclysmModifierSource::EmpireTree));
	TestEqual(TEXT("no player node: no line"),
			  UCataclysmEmpireTree::PlayerStatModifiersFor(FCataclysmEmpireTreePlayerBonuses(), State).Num(), 0);
	return true;
}

/**
 * The dungeon game mode puts the tree on the character at every floor and takes it off when the player leaves: with
 * no tree a floor leaves magic find at 0 and loot quantity at 100; with Eyes of the Empire, Bounty, Pathfinding and
 * Manifest Wealth, floor 1 gives 50 magic find, 175 loot quantity and 20% more movement speed, which the drop roll
 * reads and a refresh made for another reason keeps; floor 3 adds Manifest Wealth's 2 x 25; leaving takes all of it off.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootGameModeTest,
	"Cataclysm.EmpireTreeLoot.TheGameModePutsTheTreeOnTheCharacterInsideADungeonOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootGameModeTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;
	using Combat = UCataclysmCombatAttributeSet;

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
	UCataclysmEmpireRun* Run = QuietLootRun(2741);
	Mode->SetEmpireRunForTests(Run);

	// ONE ORDINARY DUNGEON OF TWENTY FLOORS ON THE FIRST STANDING CITY, put there by hand with its timer.
	const FCataclysmCity* City = nullptr;
	for (const FCataclysmCity& Each : Run->Map->Cities)
	{
		if (!Each.bFallen)
		{
			City = &Each;
			break;
		}
	}
	if (!TestNotNull(TEXT("set-up: a standing city"), City))
	{
		return false;
	}
	FCataclysmDungeon Dungeon = LootDungeon(ECataclysmDungeonType::Basic, 20, 0.0f);
	Dungeon.DungeonId = Run->NextDungeonId;
	Dungeon.CityId = City->CityId;
	Dungeon.CityTier = City->Tier;
	Dungeon.ResolveDays = 40.0f;
	Dungeon.SpawnedDay = Run->Day();
	Run->NextDungeonId = Dungeon.DungeonId + 1;
	Run->Dungeons.Add(Dungeon);
	if (!TestTrue(TEXT("set-up: its timer"), Run->Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors)
				  && Run->Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays))
		|| !TestTrue(TEXT("set-up: the dungeon was entered"), Mode->EnterEmpireDungeon(Dungeon.DungeonId)))
	{
		return false;
	}

	FTreeLootCharacter Character(World);
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);
	const float BareSpeed = Character.Read(Combat::GetMovementSpeedAttribute());

	// NO TREE: the game mode asks a game instance a test world does not have, and puts nothing on.
	Mode->FloorNumber = 1;
	Mode->ApplyFloorRulesTo(Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("no tree: magic find 0"), Character.Read(Combat::GetMagicFindAttribute()), 0.0f, 0.01f);
	TestEqual(TEXT("no tree: loot quantity 100"), Character.Read(Combat::GetLootQuantityAttribute()), 100.0f, 0.01f);
	TestEqual(TEXT("no tree: no line held"), Character.AbilitySystem->GetEmpireTreeStatModifiers().Num(), 0);

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.ExplorerMagicFind = 0.50f;
	Bonuses.ExplorerLootQuantity = 0.75f;
	Bonuses.MovementSpeed = 0.20f;
	Bonuses.MagicFindPerFloorCleared = 0.25f;
	Mode->SetEmpireTreePlayerBonusesForTests(Bonuses);
	Mode->ApplyFloorRulesTo(Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("floor 1: 50 magic find"), Character.Read(Combat::GetMagicFindAttribute()), 50.0f, 0.01f);
	TestEqual(TEXT("floor 1: loot quantity 175"), Character.Read(Combat::GetLootQuantityAttribute()), 175.0f, 0.01f);
	TestEqual(TEXT("floor 1: 20% more movement speed"), Character.Read(Combat::GetMovementSpeedAttribute()),
			  BareSpeed * 1.2f, 0.01f);

	float MagicFind = 0.0f;
	float LootQuantity = 0.0f;
	UCataclysmDropSpawner::LootStatsOf(Character.Actor, MagicFind, LootQuantity);
	TestEqual(TEXT("the drop roll reads the 50"), MagicFind, 50.0f, 0.01f);
	TestEqual(TEXT("and the 175"), LootQuantity, 175.0f, 0.01f);

	// A REFRESH MADE FOR ANOTHER REASON -- a helmet changed -- KEEPS THEM.
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);
	TestEqual(TEXT("still 50 after a refresh made for another reason"), Character.Read(Combat::GetMagicFindAttribute()),
			  50.0f, 0.01f);

	Mode->FloorNumber = 3;
	Mode->ApplyFloorRulesTo(Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("floor 3: two floors cleared add 50"), Character.Read(Combat::GetMagicFindAttribute()), 100.0f, 0.01f);

	Mode->LeaveEmpireDungeon();
	Mode->ApplyFloorRulesTo(Character.AbilitySystem, Character.Equipment);
	TestEqual(TEXT("left: magic find 0"), Character.Read(Combat::GetMagicFindAttribute()), 0.0f, 0.01f);
	TestEqual(TEXT("left: loot quantity 100"), Character.Read(Combat::GetLootQuantityAttribute()), 100.0f, 0.01f);
	TestEqual(TEXT("left: the bare movement speed"), Character.Read(Combat::GetMovementSpeedAttribute()), BareSpeed, 0.01f);
	TestEqual(TEXT("left: no line held"), Character.AbilitySystem->GetEmpireTreeStatModifiers().Num(), 0);
	return true;
}

/**
 * Through the game mode, the boss dungeon and a Fallen City read their own default depths and the run's kinds: a game
 * mode walking a dungeon with Quality over Quantity and Dungeon Mastery gives what `DungeonStateFor` says for it, so
 * the state the floor is worked from is the bound dungeon's and not a stale or empty one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeLootBoundTest,
	"Cataclysm.EmpireTreeLoot.TheGameModeReadsTheBoundDungeonAndTheRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeLootBoundTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeLootTest;

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
	UCataclysmEmpireRun* Run = QuietLootRun(2741);
	Mode->SetEmpireRunForTests(Run);
	Run->DungeonsCleared = 3;
	Run->BasicDungeonsCleared = 2;
	Run->QuestObjectives = 1;

	TestEqual(TEXT("no dungeon bound: no line"), Mode->EmpireTreeStatModifiersHere().Num(), 0);

	const FCataclysmCity& City = Run->Map->Cities[0];
	FCataclysmDungeon Dungeon = LootDungeon(ECataclysmDungeonType::Basic, 20, 0.0f, -7);
	Dungeon.DungeonId = Run->NextDungeonId;
	Dungeon.CityId = City.CityId;
	Dungeon.CityTier = City.Tier;
	Dungeon.ResolveDays = 40.0f;
	Dungeon.SpawnedDay = Run->Day();
	Run->NextDungeonId = Dungeon.DungeonId + 1;
	Run->Dungeons.Add(Dungeon);
	if (!TestTrue(TEXT("set-up: its timer"), Run->Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors)
				  && Run->Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays))
		|| !TestTrue(TEXT("set-up: the dungeon was entered"), Mode->EnterEmpireDungeon(Dungeon.DungeonId)))
	{
		return false;
	}

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.MagicFindPerFloorRemoved = 0.02f;
	Bonuses.DamagePerKindDefeated = 0.30f;
	Mode->SetEmpireTreePlayerBonusesForTests(Bonuses);
	const TMap<FName, TArray<FCataclysmStatModifier>> Lines = Mode->EmpireTreeStatModifiersHere();
	const TArray<FCataclysmStatModifier>* MagicFind = Lines.Find(FName(TEXT("magic_find")));
	const TArray<FCataclysmStatModifier>* Attack = Lines.Find(FName(TEXT("attack_damage")));
	if (TestTrue(TEXT("magic find from 7 floors removed"), MagicFind && MagicFind->Num() == 1))
	{
		TestEqual(TEXT("14 magic find"), (*MagicFind)[0].Value, 14.0f, 0.01f);
	}
	if (TestTrue(TEXT("damage from two kinds"), Attack && Attack->Num() == 1))
	{
		TestEqual(TEXT("+60% attack damage"), (*Attack)[0].Value, 60.0f, 0.01f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
