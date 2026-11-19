// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * The empire upgrade tree's city nodes act on the empire. Issue #2741 slice 3a: the 31 nodes and options the
 * coordinating session's S2 of 2026-11-18 gave this slice -- maximum defence, population, the damage a dungeon does to
 * a city, the days on a resolve timer, and the gap between surges.
 *
 * `Cataclysm.EmpireTreeCity.` IS A NEW GROUP and selects these and nothing else.
 *
 * MOSTLY A RUN MADE BY HAND, handed bonuses through `UCataclysmEmpireRun::SetTreeBonuses`, which is the one door the game
 * uses; the two tests at the end go through the game instance in a world with a player, as slice 1b's do. THE REAL
 * TABLES: every allocation is added up through `DT_EmpireTreeEffects` and `DT_EmpireTreeNodes`.
 *
 * WHAT IS NOT HERE: that `UCataclysmGameInstance::BeginACharactersEmpireRun` hands a new run the tree before it begins, and
 * that a load hands the loaded run its character's tree. A game instance a test makes has no world of its own, so the
 * first cannot read a player there, and the second needs the whole load. Both calls are held in place by
 * `tools/tests/test_empire_tree_bonuses_are_handed_to_every_run.py`; what each call does is tested here.
 */
namespace CataclysmEmpireTreeCityTest
{
	// The tree file's identifiers, by the node names the table gives them.
	const FName Foundation(TEXT("basic_0000000v"));			// +5% city defence a point, 10 points
	const FName StructuralIntegrity(TEXT("basic_0000000w"));	// -1.5% damage a point, 10 points
	const FName UrbanPlanning(TEXT("basic_0000000x"));			// +5% Outpost base population a point, 10 points
	const FName FortifiedGates(TEXT("basic_arch_t1d"));			// +8% Outpost defence a point, 8 points
	const FName BorderPatrol(TEXT("basic_arch_t1g"));			// +1 day on Outposts a point, 5 points
	const FName EmergencyShelters(TEXT("keystone_0000000y"));	// +3 days on Outposts
	const FName MasonryTechniques(TEXT("basic_00000010"));		// +10% Bulwarks and Sanctuaries a point, 15 points
	const FName UrbanFortification(TEXT("basic_00000011"));		// -4% damage a point, 15 points
	const FName ImperialCensus(TEXT("basic_00000012"));			// +10% Bulwark base population a point, 10 points
	const FName CivilDefense(TEXT("basic_arch_t2d"));			// -1.5% population damage a point, 10 points
	const FName SupplyLines(TEXT("basic_arch_t2e"));			// -1.5% damage, ring 1, a point, 10 points
	const FName ReinforcedWalls(TEXT("basic_arch_t2f"));		// +8% every city a point, 15 points
	const FName MetropolitanGrowth(TEXT("basic_00000016"));		// +15% base population a point, 10 points
	const FName SiegeResistance(TEXT("basic_arch_t3c"));		// -1.33% damage a point, 15 points
	const FName MonumentBuilding(TEXT("basic_arch_t3e"));		// +10% Sanctuaries a point, 10 points
	const FName StrategicReserve(TEXT("basic_arch_t3f"));		// +1 day every timer a point, 10 points
	const FName MartialLaw(TEXT("keystone_00000017"));			// +5 days below a tenth of defence
	const FName ImperialDecree(TEXT("keystone_00000018"));		// +20% defence, -10% population
	const FName BastionSpirit(TEXT("basic_0000001a"));			// +1 day a point per 20 Explorer points, capped at 15
	const FName SovereignsMight(TEXT("basic_0000001b"));		// -1% damage a point, 25 points
	const FName ManifestDestiny(TEXT("basic_0000001c"));		// +2% total population a point, compounded, 20 points
	const FName UnyieldingDefense(TEXT("basic_arch_t4d"));		// -0.5% a point per active type, 5 points
	const FName IronWill(TEXT("basic_arch_t4e"));				// -1% a point at a full empire, 15 points
	const FName FortifiedPillar(TEXT("basic_arch_t4f"));		// -2% population damage, rings 0 to 2, a point, 10 points
	const FName ImperialCommand(TEXT("keystone_0000001d"));		// 40% less for a city with 2+ upgrades
	const FName BeaconOfHope(TEXT("keystone_0000001e"));		// +50% defence, rings 0 to 2
	const FName RoyalEngineer(TEXT("capstone_0000001i"));		// +2% every city per city with a defensive upgrade
	const FName GoldenAge(TEXT("basic_00000046"));				// +1% total population a point per 20 Architect points, capped at 40%
	const FName Pacing(TEXT("basic_00000029"));					// an Explorer node, for Bastion Spirit's steps
	const FName Foundations(TEXT("capstone_0000000m"));			// option 1, The Aegis of Hope: +50% Outpost defence
	const FName Edicts(TEXT("capstone_0000000n"));				// option 1, The Sentinel: 10% less from the commonest type
	const FName Zenith(TEXT("capstone_0000000p"));				// option 3, The Flood Barrier: +20 days on every gap

	/** What an allocation adds up to, through the generated tables. */
	FCataclysmEmpireTreeBonuses BonusesOf(const FCataclysmPassiveAllocation& Tree)
	{
		return UCataclysmEmpireTree::CityBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
													UCataclysmEmpireTree::LoadEffectTable(), Tree);
	}

	/** One allocation holding one node at these points. */
	FCataclysmPassiveAllocation Holding(FName Node, int32 Points)
	{
		FCataclysmPassiveAllocation Tree;
		Tree.Add(Node, Points);
		return Tree;
	}

	/**
	 * A run begun with these bonuses, its first day spent and its board emptied, as `CataclysmEmpireRunTests.cpp`'s
	 * `MakeEmptyRun` leaves one: the next surge 90 days off, nothing standing, every city as day 1 left it.
	 */
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

	/** The first standing city of a tier, or null. */
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

	/** Puts a dungeon on a city with a timer, as a surge would, with the damage figures given. */
	void Place(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type, ECataclysmDungeonSubType SubType,
			   const FCataclysmCity& City, float ResolveDays, ECataclysmType Sender = ECataclysmType::None)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Cataclysm = Sender;
		Dungeon.Floors = 20;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.SpawnedDay = Run.Day();
		Dungeon.DefenceDamage = 100.0f;
		Dungeon.PopulationDamage = 400.0f;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/** What a day's hits of one kind took off one city's defence and people. */
	void TakenBy(const FCataclysmDayReport& Report, int32 CityId, ECataclysmCityHitKind Kind, float& OutDefence,
				 float& OutPopulation)
	{
		OutDefence = 0.0f;
		OutPopulation = 0.0f;
		for (const FCataclysmCityHit& Hit : Report.Hits)
		{
			if (Hit.CityId == CityId && Hit.Kind == Kind)
			{
				OutDefence += Hit.DefenceLost;
				OutPopulation += Hit.PopulationLost;
			}
		}
	}

	/** The record the writer would make of `Run`, written to JSON and read back, as the run restore tests make it. */
	UCataclysmRunSave* SavedAndReadBack(const UCataclysmEmpireRun& Run, FString& OutJson, FString& OutWhy)
	{
		UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
		Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
		FCataclysmSaveGather::RunClockFrom(Run, *Record);
		FCataclysmSaveGather::CitiesFrom(Run, *Record);
		FCataclysmSaveGather::SurgeScheduleFrom(Run, *Record);
		FCataclysmSaveGather::ProgressFrom(Run, *Record);
		FCataclysmSaveGather::IdentityFrom(Run, *Record);
		if (!FCataclysmSaveGather::DungeonsFrom(Run, *Record, OutWhy) || !FCataclysmSaveStorage::ToJson(Record, OutJson, OutWhy))
		{
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		return Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			OutJson, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, OutWhy));
	}

	/** A world with this project's game instance and a possessed player of a Standard, offline partition. */
	struct FCityTreeScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State; }
		UCataclysmAccountSave* Account() const { return Instance != nullptr ? Instance->AccountFor(Partition) : nullptr; }
	};

	FCityTreeScene MakeScene()
	{
		FCityTreeScene Out;
		Out.Partition.Lethality = ECataclysmLethality::Standard;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = false;

		// THE TEST'S OWN SLOT, emptied first, as slice 1b's scenes empty it.
		const FString Slot = UCataclysmGameInstance::AccountSlotFor(Out.Partition);
		if (!Slot.IsEmpty())
		{
			const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
			while (FCataclysmSaveStorage::HasAWriteInFlight(Slot) && FPlatformTime::Seconds() < GiveUpAt)
			{
				FPlatformProcess::Sleep(0.01f);
			}
			if (UGameplayStatics::DoesSaveGameExist(Slot, UCataclysmSaveWriter::UserIndex))
			{
				UGameplayStatics::DeleteGameInSlot(Slot, UCataclysmSaveWriter::UserIndex);
			}
		}

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);

		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Character);
			Out.State->SetIdentity(TEXT("Citywright"), Out.Partition);
		}
		else
		{
			Out.State = nullptr;
		}
		return Out;
	}
}

// NO `using namespace` AT FILE SCOPE, for the reason `CataclysmEmpireTreeTests.cpp` gives. Each test opens it inside.

// ---------------------------------------------------------------------------
// What the bought nodes add up to
// ---------------------------------------------------------------------------

/**
 * Nothing bought is no bonus: every factor one and every number of days nought, and a run handed that is the run the game
 * had before -- its Outposts at 1000 of 1000. The control the rest of the group leans on. Prints "NONE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityNoneTest,
	"Cataclysm.EmpireTreeCity.ATreeWithNothingBoughtChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityNoneTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	if (!TestNotNull(TEXT("set-up: the effect table loads"), UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	const FCataclysmEmpireTreeBonuses None = BonusesOf(FCataclysmPassiveAllocation());
	const FCataclysmTreeDamageFactors Whole = None.DamageFactorsFor(1, 8, 0.5f, 3, true);
	AddInfo(FString::Printf(TEXT("NONE: defence x%.3f, population x%.3f, hit %.3f / %.3f, days %.1f, gap %.1f"),
							None.MaxDefenceFactorFor(ECataclysmCityTier::Sanctuary, 1, 5),
							None.MaxPopulationFactorFor(ECataclysmCityTier::Outpost), Whole.Defence, Whole.Population,
							None.ResolveDaysFor(ECataclysmCityTier::Outpost, 0.05f), None.SurgeGapDays));
	TestEqual(TEXT("maximum defence x1"), None.MaxDefenceFactorFor(ECataclysmCityTier::Sanctuary, 1, 5), 1.0f);
	TestEqual(TEXT("maximum population x1"), None.MaxPopulationFactorFor(ECataclysmCityTier::Outpost), 1.0f);
	TestEqual(TEXT("the whole hit on defence"), Whole.Defence, 1.0f);
	TestEqual(TEXT("and on people"), Whole.Population, 1.0f);
	TestEqual(TEXT("no days on a timer"), None.ResolveDaysFor(ECataclysmCityTier::Outpost, 0.05f), 0.0f);
	TestEqual(TEXT("none on the gap"), None.SurgeGapDays, 0.0f);

	UCataclysmEmpireRun* Run = QuietRun(2741, None);
	FCataclysmCity* Outpost = Run ? FirstOf(*Run, ECataclysmCityTier::Outpost) : nullptr;
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: an Outpost's maximum is its tier's"), Run->Map->EffectiveMaxDefence(*Outpost),
			  UCataclysmEmpireMap::OutpostMaxDefence);
	return true;
}

/**
 * The Architect branch's city-health and damage nodes at full points, added up through the table, give the
 * simulation's `TREE_ARCHITECT_AS_DESIGNED` for its scenario, a Sanctuary next to the Pillar holding two upgrades: x5.90
 * maximum defence; the damage product less Global Vigilance (slice 3b's) at its cap, with Civil Defense and Fortified
 * Pillar on population only (the simulation's one factor takes both on both); and 0.995^5 for each active type. The day
 * nodes: 10 on every timer, 8 on an Outpost's, 5 below a tenth. Prints "ARCHITECT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityArchitectTest,
	"Cataclysm.EmpireTreeCity.TheArchitectBranchGivesTheSimulationsPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityArchitectTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	FCataclysmPassiveAllocation Tree;
	for (const TPair<FName, int32>& Node : TArray<TPair<FName, int32>>({
			 { Foundation, 10 }, { ReinforcedWalls, 15 }, { MasonryTechniques, 15 }, { MonumentBuilding, 10 },
			 { BeaconOfHope, 1 }, { ImperialDecree, 1 }, { StructuralIntegrity, 10 }, { UrbanFortification, 15 },
			 { CivilDefense, 10 }, { SupplyLines, 10 }, { SiegeResistance, 15 }, { SovereignsMight, 25 },
			 { UnyieldingDefense, 5 }, { FortifiedPillar, 10 }, { IronWill, 15 }, { ImperialCommand, 1 },
			 { StrategicReserve, 10 }, { BorderPatrol, 5 }, { EmergencyShelters, 1 }, { MartialLaw, 1 } }))
	{
		Tree.Add(Node.Key, Node.Value);
	}
	const FCataclysmEmpireTreeBonuses Bonuses = BonusesOf(Tree);

	// THE SIMULATION'S NUMBERS, `sim/cataclysm_sim/config.py`: city_health_mult 5.90; city_damage_mult 0.0766 / 0.995^5,
	// which includes Global Vigilance at 0.85; city_damage_mult_per_type 0.995^5.
	const float SimulationHealth = 5.90f;
	const float SimulationDamage = 0.0766f / FMath::Pow(0.995f, 5.0f);
	const float WithoutGlobalVigilance = SimulationDamage / 0.85f;
	const float PopulationOnlyNodes = FMath::Pow(0.985f, 10.0f) * FMath::Pow(0.98f, 10.0f);

	const float Health = Bonuses.MaxDefenceFactorFor(ECataclysmCityTier::Sanctuary, 1, 0);
	const FCataclysmTreeDamageFactors Hit = Bonuses.DamageFactorsFor(1, 0, 1.0f, 2, false);
	const float PerType = Bonuses.DamageFactorsFor(1, 1, 1.0f, 2, false).Defence / Hit.Defence;

	AddInfo(FString::Printf(TEXT("ARCHITECT: Sanctuary in ring 1 x%.4f (simulation %.2f); hit on people %.6f, on defence "
								 "%.6f (simulation %.6f without Global Vigilance); a type %.6f; days %.0f / %.0f / %.0f"),
							Health, SimulationHealth, Hit.Population, Hit.Defence, WithoutGlobalVigilance, PerType,
							Bonuses.ResolveDaysAll, Bonuses.ResolveDaysOutposts, Bonuses.ResolveDaysBelowTenPercent));
	TestEqual(TEXT("a Sanctuary next to the Pillar: the simulation's x5.90"), Health, SimulationHealth, 0.0005f);
	TestEqual(TEXT("the hit on its people: the simulation's product less Global Vigilance"), Hit.Population,
			  WithoutGlobalVigilance, WithoutGlobalVigilance * 0.002f);
	TestEqual(TEXT("the hit on its defence: the same less the two population-only nodes"), Hit.Defence,
			  WithoutGlobalVigilance / PopulationOnlyNodes, WithoutGlobalVigilance * 0.002f);
	TestEqual(TEXT("and 0.995^5 for each active type"), PerType, FMath::Pow(0.995f, 5.0f), 0.00001f);
	TestEqual(TEXT("Strategic Reserve: 10 days on every timer"), Bonuses.ResolveDaysAll, 10.0f);
	TestEqual(TEXT("Border Patrol and Emergency Shelters: 8 on an Outpost's"), Bonuses.ResolveDaysOutposts, 8.0f);
	TestEqual(TEXT("Martial Law: 5 below a tenth"), Bonuses.ResolveDaysBelowTenPercent, 5.0f);
	TestEqual(TEXT("Imperial Decree takes 10% off population"), Bonuses.MaxPopulationFactorFor(ECataclysmCityTier::Pillar),
			  0.9f, 0.00001f);
	return true;
}

/**
 * A centre capstone's option acts while it is the chosen one and the capstone's point is bought: The Aegis of Hope gives
 * Outposts +50% only then; another option chosen gives nothing of it. The Sentinel and The Flood Barrier the same way.
 * Population: Urban Planning and Imperial Census by tier, Metropolitan Growth everywhere, Manifest Destiny compounded per
 * point. Prints "OPTIONS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityOptionsTest,
	"Cataclysm.EmpireTreeCity.AnOptionActsWhileChosenAndPopulationNodesReachTheirCities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityOptionsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	FCataclysmPassiveAllocation ChosenNoPoint;
	ChosenNoPoint.SetChosenOption(Foundations, 1);
	FCataclysmPassiveAllocation Chosen = ChosenNoPoint;
	Chosen.Add(Foundations, 1);
	FCataclysmPassiveAllocation Other;
	Other.SetChosenOption(Foundations, 2);
	Other.Add(Foundations, 1);
	FCataclysmPassiveAllocation Three;
	Three.SetChosenOption(Edicts, 1);
	Three.Add(Edicts, 1);
	Three.SetChosenOption(Zenith, 3);
	Three.Add(Zenith, 1);

	const float NoPoint = BonusesOf(ChosenNoPoint).MaxDefenceOutposts;
	const float WithPoint = BonusesOf(Chosen).MaxDefenceOutposts;
	const float OtherOption = BonusesOf(Other).MaxDefenceOutposts;
	const FCataclysmEmpireTreeBonuses Both = BonusesOf(Three);

	FCataclysmPassiveAllocation People;
	People.Add(UrbanPlanning, 10);
	People.Add(ImperialCensus, 10);
	People.Add(MetropolitanGrowth, 10);
	People.Add(ManifestDestiny, 20);
	const FCataclysmEmpireTreeBonuses Population = BonusesOf(People);
	const float Manifest = FMath::Pow(1.02f, 20.0f);

	AddInfo(FString::Printf(TEXT("OPTIONS: Aegis %.2f / %.2f / %.2f; Sentinel %.2f; Flood Barrier %.0f; population "
								 "Outpost x%.4f, Bulwark x%.4f, Sanctuary x%.4f"),
							NoPoint, WithPoint, OtherOption, Both.DamageTakenFromCommonestType, Both.SurgeGapDays,
							Population.MaxPopulationFactorFor(ECataclysmCityTier::Outpost),
							Population.MaxPopulationFactorFor(ECataclysmCityTier::Bulwark),
							Population.MaxPopulationFactorFor(ECataclysmCityTier::Sanctuary)));
	TestEqual(TEXT("The Aegis of Hope chosen with no point bought gives nothing"), NoPoint, 0.0f);
	TestEqual(TEXT("with its point, Outposts +50%"), WithPoint, 0.5f, 0.00001f);
	TestEqual(TEXT("another option chosen gives none of it"), OtherOption, 0.0f);
	TestEqual(TEXT("The Sentinel: 10% less from the commonest type"), Both.DamageTakenFromCommonestType, 0.9f, 0.00001f);
	TestEqual(TEXT("The Flood Barrier: 20 days on the gap"), Both.SurgeGapDays, 20.0f);
	TestEqual(TEXT("an Outpost: +50% Urban Planning, +150% Metropolitan Growth, then Manifest Destiny"),
			  Population.MaxPopulationFactorFor(ECataclysmCityTier::Outpost), 3.0f * Manifest, 0.001f);
	TestEqual(TEXT("a Bulwark: +100% Imperial Census instead"), Population.MaxPopulationFactorFor(ECataclysmCityTier::Bulwark),
			  3.5f * Manifest, 0.001f);
	TestEqual(TEXT("a Sanctuary: Metropolitan Growth only"), Population.MaxPopulationFactorFor(ECataclysmCityTier::Sanctuary),
			  2.5f * Manifest, 0.001f);
	return true;
}

/**
 * Golden Age and Bastion Spirit count whole steps of 20 points in another branch (C3) and stop at their caps (S4, S3):
 * Golden Age 15 points with 40 Architect points is +30%, with 60 it would be +45% and is held at +40%; Bastion Spirit 2
 * points with 100 Explorer points is 10 days, 4 points would be 20 and is held at 15; 19 points in a branch is no step.
 * Prints "CAPS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityCapsTest,
	"Cataclysm.EmpireTreeCity.GoldenAgeAndBastionSpiritCountStepsAndStopAtTheirCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityCapsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	auto Golden = [](int32 ArchitectPoints)
	{
		FCataclysmPassiveAllocation Tree = Holding(GoldenAge, 15);
		Tree.Add(Foundation, ArchitectPoints); // One node holding them all: only the branch's total is counted.
		return BonusesOf(Tree).TotalPopulationFactor;
	};
	auto Bastion = [](int32 Points, int32 ExplorerPoints)
	{
		FCataclysmPassiveAllocation Tree = Holding(BastionSpirit, Points);
		Tree.Add(Pacing, ExplorerPoints);
		return BonusesOf(Tree).ResolveDaysAll;
	};

	const float Golden19 = Golden(19);
	const float Golden40 = Golden(40);
	const float Golden60 = Golden(60);
	const float Bastion19 = Bastion(4, 19);
	const float Bastion2 = Bastion(2, 100);
	const float Bastion4 = Bastion(4, 100);
	AddInfo(FString::Printf(TEXT("CAPS: Golden Age with 19 / 40 / 60 Architect points x%.3f / x%.3f / x%.3f; Bastion Spirit "
								 "4 at 19, 2 at 100, 4 at 100: %.0f / %.0f / %.0f days"),
							Golden19, Golden40, Golden60, Bastion19, Bastion2, Bastion4));
	TestEqual(TEXT("19 Architect points is no step"), Golden19, 1.0f, 0.00001f);
	TestEqual(TEXT("40 is two: +30%"), Golden40, 1.3f, 0.00001f);
	TestEqual(TEXT("60 would be +45% and is held at +40%"), Golden60, 1.4f, 0.00001f);
	TestEqual(TEXT("19 Explorer points is no step"), Bastion19, 0.0f);
	TestEqual(TEXT("2 points at 100 Explorer points: 10 days"), Bastion2, 10.0f);
	TestEqual(TEXT("4 would be 20 and is held at 15"), Bastion4, 15.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Maximum defence and what a city holds (S1)
// ---------------------------------------------------------------------------

/**
 * A run begun with the tree builds every city full at the raised maximum, as the simulation's `build_empire` does, while
 * the saved base stays the tier's: with Foundation and Fortified Gates full, an Outpost holds 2140 of 2140 and its
 * `MaxDefence` is 1000. Prints "BEGUN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityBegunTest,
	"Cataclysm.EmpireTreeCity.ARunBegunWithTheTreeBuildsItsCitiesFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityBegunTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	FCataclysmPassiveAllocation Tree = Holding(Foundation, 10);
	Tree.Add(FortifiedGates, 8);
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->SetTreeBonuses(BonusesOf(Tree));
	Run->Begin(2741);
	const FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	const FCataclysmCity* Pillar = FirstOf(*Run, ECataclysmCityTier::Pillar);
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost) || !TestNotNull(TEXT("set-up: the Pillar"), Pillar))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("BEGUN: Outpost %.0f of %.0f, base %.0f; Pillar %.0f of %.0f"), Outpost->Defence,
							Run->Map->EffectiveMaxDefence(*Outpost), Outpost->MaxDefence, Pillar->Defence,
							Run->Map->EffectiveMaxDefence(*Pillar)));
	TestEqual(TEXT("an Outpost's maximum: 1000 x (1 + 0.5 + 0.64)"), Run->Map->EffectiveMaxDefence(*Outpost), 2140.0f, 0.01f);
	TestEqual(TEXT("and it holds all of it"), Outpost->Defence, 2140.0f, 0.01f);
	TestEqual(TEXT("its saved base is the tier's"), Outpost->MaxDefence, UCataclysmEmpireMap::OutpostMaxDefence);
	TestEqual(TEXT("the Pillar has Foundation and not Fortified Gates"), Pillar->Defence,
			  UCataclysmEmpireMap::PillarMaxDefence * 1.5f, 0.01f);
	return true;
}

/**
 * The coordinating session's S1: a point bought mid-run raises the maximum and leaves what a city holds where it is. A
 * full Outpost reads "1000 of 1050" on its screen and 96% on the map after one point of Foundation. Prints "RAISED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityRaisedTest,
	"Cataclysm.EmpireTreeCity.ARaisedMaximumLeavesWhatACityHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityRaisedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost))
	{
		return false;
	}
	Outpost->Defence = Outpost->MaxDefence; // Full, whatever day 1 did.
	const int32 OutpostId = Outpost->CityId;

	Run->SetTreeBonuses(BonusesOf(Holding(Foundation, 1)));
	const FString Screen = UCataclysmCityScreenLayout::StatusTextFor(Run, OutpostId);
	const FString Label = UCataclysmEmpireMapLayout::CityLabel(Run->Map, OutpostId);
	AddInfo(FString::Printf(TEXT("RAISED: \"%s\" / \"%s\""), *Screen, *Label));
	TestEqual(TEXT("the maximum rose to 1050"), Run->Map->EffectiveMaxDefence(*Outpost), 1050.0f, 0.01f);
	TestEqual(TEXT("what the Outpost holds did not"), Run->Map->DefenceOf(*Outpost), 1000.0f, 0.01f);
	TestTrue(TEXT("its screen says 1000 of 1050"), Screen.Contains(TEXT("defence 1000 of 1050 (95%)")));
	TestEqual(TEXT("the map says 96%"), Label, FString(TEXT("Outpost 96%")));
	return true;
}

/**
 * A lowered maximum -- a respec -- is read as the smaller of the two, and nothing is written: a city built full at 1500
 * reads 1000 of 1000 once the tree is gone, and a hit of 100 then leaves 900, not 1400, and the day's record says 100
 * was taken. Prints "LOWERED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityLoweredTest,
	"Cataclysm.EmpireTreeCity.ALoweredMaximumIsReadAsTheSmallerAndAHitStartsThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityLoweredTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmEmpireRun* Run = QuietRun(2741, BonusesOf(Holding(Foundation, 10)));
	FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost))
	{
		return false;
	}
	Outpost->Defence = Run->Map->EffectiveMaxDefence(*Outpost); // 1500, full.
	const int32 OutpostId = Outpost->CityId;

	Run->SetTreeBonuses(FCataclysmEmpireTreeBonuses());
	const float Stored = Outpost->Defence;
	const float Read = Run->Map->DefenceOf(*Outpost);

	Place(*Run, 9001, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Outpost, 1.0f);
	Run->Dungeons.Last().DefenceDamage = 100.0f / Run->Dungeons.Last().BiteScale(); // A hit of exactly 100.
	const FCataclysmDayReport Report = Run->AdvanceDay();
	float Lost = 0.0f;
	float People = 0.0f;
	TakenBy(Report, OutpostId, ECataclysmCityHitKind::Ordinary, Lost, People);
	const FCataclysmCity* After = Run->Map->Find(OutpostId);

	AddInfo(FString::Printf(TEXT("LOWERED: stored %.0f, read %.0f; after a hit of 100, %.0f, recorded %.1f"), Stored, Read,
							After ? After->Defence : -1.0f, Lost));
	TestEqual(TEXT("nothing was written: it still stores 1500"), Stored, 1500.0f, 0.01f);
	TestEqual(TEXT("it is read as 1000"), Read, 1000.0f, 0.01f);
	TestTrue(TEXT("a hit of 100 leaves 900"), After != nullptr && FMath::IsNearlyEqual(After->Defence, 900.0f, 0.5f));
	TestEqual(TEXT("and the record says 100 was taken"), Lost, 100.0f, 0.5f);
	return true;
}

// ---------------------------------------------------------------------------
// What a dungeon's hit takes
// ---------------------------------------------------------------------------

/**
 * The tree's reductions apply at all three places a dungeon damages a city, as the simulation's `damage_taken` does:
 * an ordinary resolve, a Quest dungeon's timer and a Siege's daily share. Two runs on one seed take the same hit, one
 * holding Structural Integrity and Civil Defense in full; each hit takes 0.985^10 of the defence and 0.985^20 of the
 * people. Prints "HITS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityHitsTest,
	"Cataclysm.EmpireTreeCity.EveryHitADungeonDoesIsReduced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityHitsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	FCataclysmPassiveAllocation Tree = Holding(StructuralIntegrity, 10);
	Tree.Add(CivilDefense, 10);
	const float DefenceKept = FMath::Pow(0.985f, 10.0f);
	const float PeopleKept = DefenceKept * FMath::Pow(0.985f, 10.0f);

	struct FCase
	{
		const TCHAR* What;
		ECataclysmDungeonType Type;
		ECataclysmDungeonSubType SubType;
		float ResolveDays;
		ECataclysmCityHitKind Kind;
	};
	const FCase Cases[] = {
		{ TEXT("an ordinary resolve"), ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, 1.0f,
		  ECataclysmCityHitKind::Ordinary },
		{ TEXT("a Quest dungeon's timer"), ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::None, 1.0f,
		  ECataclysmCityHitKind::QuestTimer },
		{ TEXT("a Siege's daily share"), ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, 10000.0f,
		  ECataclysmCityHitKind::SiegeShare },
	};

	for (const FCase& Case : Cases)
	{
		float Taken[2][2] = {};
		for (int32 WithTree = 0; WithTree < 2; ++WithTree)
		{
			UCataclysmEmpireRun* Run =
				QuietRun(2741, WithTree ? BonusesOf(Tree) : FCataclysmEmpireTreeBonuses());
			const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
			if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
			{
				return false;
			}
			const int32 BulwarkId = Bulwark->CityId;
			Place(*Run, 9002, Case.Type, Case.SubType, *Bulwark, Case.ResolveDays);
			TakenBy(Run->AdvanceDay(), BulwarkId, Case.Kind, Taken[WithTree][0], Taken[WithTree][1]);
		}
		AddInfo(FString::Printf(TEXT("HITS: %s took %.2f / %.2f without the tree and %.2f / %.2f with it"), Case.What,
								Taken[0][0], Taken[0][1], Taken[1][0], Taken[1][1]));
		if (!TestTrue(FString::Printf(TEXT("set-up: %s took something"), Case.What), Taken[0][0] > 0.0f))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s takes 0.985^10 of the defence"), Case.What), Taken[1][0] / Taken[0][0],
				  DefenceKept, 0.0005f);
		TestEqual(FString::Printf(TEXT("%s takes 0.985^20 of the people"), Case.What), Taken[1][1] / Taken[0][1],
				  PeopleKept, 0.0005f);
	}
	return true;
}

/**
 * A Siege takes its share of the maximum WITH the tree, as the simulation's Siege takes its share of a `max_defense` the
 * tree has raised: Reinforced Walls in full doubles and a fifth an Outpost's maximum, and the Siege's first day takes
 * 2.2 times its 1% of it, plus the day's growth. The owner's exception of 2026-09-05 -- a Siege does not care how thick
 * the walls are -- holds against the tree as it does against a city upgrade. Prints "SIEGE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCitySiegeTest,
	"Cataclysm.EmpireTreeCity.ASiegeTakesItsShareOfTheRaisedMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCitySiegeTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmEmpireRun* Run = QuietRun(2741, BonusesOf(Holding(ReinforcedWalls, 15)));
	FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost))
	{
		return false;
	}
	const int32 OutpostId = Outpost->CityId;
	const float Max = Run->Map->EffectiveMaxDefence(*Outpost);
	Place(*Run, 9003, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, *Outpost, 10000.0f);
	float Taken = 0.0f;
	float People = 0.0f;
	TakenBy(Run->AdvanceDay(), OutpostId, ECataclysmCityHitKind::SiegeShare, Taken, People);
	const float Expected = Max * UCataclysmEmpireRun::SiegeDefenceBitePerDay + UCataclysmEmpireRun::SiegeDamageGrowthPerDay;
	AddInfo(FString::Printf(TEXT("SIEGE: maximum %.0f, the first day took %.2f, expected %.2f"), Max, Taken, Expected));
	TestEqual(TEXT("the maximum is 2.2 times the tier's"), Max, 2200.0f, 0.01f);
	TestEqual(TEXT("the first day takes 1% of it and a day's growth"), Taken, Expected, 0.01f);
	return true;
}

/**
 * The reductions that hang on a condition read it: Supply Lines only in ring 1; Fortified Pillar only on people, rings 0
 * to 2; Unyielding Defense once per active type; Iron Will scaled by the living share; Imperial Command only at two
 * upgrades; The Sentinel only against the commonest type. Prints "CONDITIONS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityConditionsTest,
	"Cataclysm.EmpireTreeCity.EachConditionalReductionReadsItsCondition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityConditionsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	const FCataclysmEmpireTreeBonuses Supply = BonusesOf(Holding(SupplyLines, 10));
	const FCataclysmEmpireTreeBonuses Pillar = BonusesOf(Holding(FortifiedPillar, 10));
	const FCataclysmEmpireTreeBonuses Unyielding = BonusesOf(Holding(UnyieldingDefense, 5));
	const FCataclysmEmpireTreeBonuses Iron = BonusesOf(Holding(IronWill, 15));
	const FCataclysmEmpireTreeBonuses Command = BonusesOf(Holding(ImperialCommand, 1));
	FCataclysmPassiveAllocation SentinelTree;
	SentinelTree.SetChosenOption(Edicts, 1);
	SentinelTree.Add(Edicts, 1);
	const FCataclysmEmpireTreeBonuses Sentinel = BonusesOf(SentinelTree);

	AddInfo(FString::Printf(
		TEXT("CONDITIONS: Supply Lines ring 1 %.4f, ring 2 %.4f; Fortified Pillar ring 2 people %.4f defence %.4f, ring 3 "
			 "%.4f; Unyielding 3 types %.4f; Iron Will full %.4f, half %.4f; Command 1/2 upgrades %.2f/%.2f; Sentinel "
			 "%.2f/%.2f"),
		Supply.DamageFactorsFor(1, 0, 1.0f, 0, false).Defence, Supply.DamageFactorsFor(2, 0, 1.0f, 0, false).Defence,
		Pillar.DamageFactorsFor(2, 0, 1.0f, 0, false).Population, Pillar.DamageFactorsFor(2, 0, 1.0f, 0, false).Defence,
		Pillar.DamageFactorsFor(3, 0, 1.0f, 0, false).Population, Unyielding.DamageFactorsFor(3, 3, 1.0f, 0, false).Defence,
		Iron.DamageFactorsFor(3, 0, 1.0f, 0, false).Defence, Iron.DamageFactorsFor(3, 0, 0.5f, 0, false).Defence,
		Command.DamageFactorsFor(3, 0, 1.0f, 1, false).Defence, Command.DamageFactorsFor(3, 0, 1.0f, 2, false).Defence,
		Sentinel.DamageFactorsFor(3, 0, 1.0f, 0, false).Defence, Sentinel.DamageFactorsFor(3, 0, 1.0f, 0, true).Defence));

	TestEqual(TEXT("Supply Lines: ring 1"), Supply.DamageFactorsFor(1, 0, 1.0f, 0, false).Defence,
			  FMath::Pow(0.985f, 10.0f), 0.00001f);
	TestEqual(TEXT("and not ring 2"), Supply.DamageFactorsFor(2, 0, 1.0f, 0, false).Defence, 1.0f);
	TestEqual(TEXT("Fortified Pillar: people in ring 2"), Pillar.DamageFactorsFor(2, 0, 1.0f, 0, false).Population,
			  FMath::Pow(0.98f, 10.0f), 0.00001f);
	TestEqual(TEXT("and not their defence"), Pillar.DamageFactorsFor(2, 0, 1.0f, 0, false).Defence, 1.0f);
	TestEqual(TEXT("and not ring 3"), Pillar.DamageFactorsFor(3, 0, 1.0f, 0, false).Population, 1.0f);
	TestEqual(TEXT("Unyielding Defense: once for each of three types"), Unyielding.DamageFactorsFor(3, 3, 1.0f, 0, false).Defence,
			  FMath::Pow(0.995f, 15.0f), 0.00001f);
	TestEqual(TEXT("Iron Will: 0.99^15 at a full empire"), Iron.DamageFactorsFor(3, 0, 1.0f, 0, false).Defence,
			  FMath::Pow(0.99f, 15.0f), 0.00001f);
	TestEqual(TEXT("and 0.995^15 at half"), Iron.DamageFactorsFor(3, 0, 0.5f, 0, false).Defence,
			  FMath::Pow(0.995f, 15.0f), 0.00001f);
	TestEqual(TEXT("Imperial Command: nothing at one upgrade"), Command.DamageFactorsFor(3, 0, 1.0f, 1, false).Defence, 1.0f);
	TestEqual(TEXT("and 40% less at two"), Command.DamageFactorsFor(3, 0, 1.0f, 2, false).Defence, 0.6f, 0.00001f);
	TestEqual(TEXT("The Sentinel: nothing from another type"), Sentinel.DamageFactorsFor(3, 0, 1.0f, 0, false).Defence, 1.0f);
	TestEqual(TEXT("and 10% less from the commonest"), Sentinel.DamageFactorsFor(3, 0, 1.0f, 0, true).Defence, 0.9f, 0.00001f);
	return true;
}

/**
 * The Sentinel's "the Cataclysm type with the most dungeons in that city": on a city holding two of Death's dungeons and
 * one of Famine's, Death is the commonest and Famine is not; with one of each, both are (S5). Prints "COMMONEST".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityCommonestTest,
	"Cataclysm.EmpireTreeCity.TheCommonestTypeIsCountedOnTheCityTiesIncluded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityCommonestTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
	{
		return false;
	}
	const int32 BulwarkId = Bulwark->CityId;
	Place(*Run, 9101, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 50.0f, ECataclysmType::Death);
	Place(*Run, 9102, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 50.0f, ECataclysmType::Famine);
	const bool bTiedDeath = Run->IsTheCommonestTypeOn(BulwarkId, ECataclysmType::Death);
	const bool bTiedFamine = Run->IsTheCommonestTypeOn(BulwarkId, ECataclysmType::Famine);
	Place(*Run, 9103, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 50.0f, ECataclysmType::Death);
	const bool bDeath = Run->IsTheCommonestTypeOn(BulwarkId, ECataclysmType::Death);
	const bool bFamine = Run->IsTheCommonestTypeOn(BulwarkId, ECataclysmType::Famine);
	AddInfo(FString::Printf(TEXT("COMMONEST: tied %d/%d; two to one %d/%d"), bTiedDeath, bTiedFamine, bDeath, bFamine));
	TestTrue(TEXT("one of each: Death counts"), bTiedDeath);
	TestTrue(TEXT("and so does Famine"), bTiedFamine);
	TestTrue(TEXT("two of Death's to one: Death counts"), bDeath);
	TestFalse(TEXT("and Famine does not"), bFamine);
	TestFalse(TEXT("CONTROL: a type with no dungeon there does not"), Run->IsTheCommonestTypeOn(BulwarkId, ECataclysmType::War));
	return true;
}

// ---------------------------------------------------------------------------
// Days
// ---------------------------------------------------------------------------

/**
 * The tree's days are on every timer at once and gone at once, and the clock resolves by them: Strategic Reserve and
 * Border Patrol add 15 to a timer on an Outpost and 10 to one on a Bulwark; a timer with one day left and the tree's
 * days on it does not resolve, and does on the next day once the tree is gone. Prints "TIMERS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityTimersTest,
	"Cataclysm.EmpireTreeCity.TimerDaysComeAtOnceAndGoAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityTimersTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	const FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost) || !TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
	{
		return false;
	}
	Place(*Run, 9201, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Outpost, 30.0f);
	Place(*Run, 9202, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 2.0f);

	FCataclysmPassiveAllocation Tree = Holding(StrategicReserve, 10);
	Tree.Add(BorderPatrol, 5);
	Run->SetTreeBonuses(BonusesOf(Tree));
	const float OutpostWith = Run->Clock->DaysUntilResolveFor(9201);
	const float BulwarkWith = Run->Clock->DaysUntilResolveFor(9202);
	const FCataclysmDayReport First = Run->AdvanceDay(); // The Bulwark's timer: 1 left of its own, 11 with the tree.
	const bool bHeld = !First.Resolved.Contains(9202);

	Run->SetTreeBonuses(FCataclysmEmpireTreeBonuses());
	const float OutpostWithout = Run->Clock->DaysUntilResolveFor(9201);
	const FCataclysmDayReport Second = Run->AdvanceDay();
	const bool bResolved = Second.Resolved.Contains(9202);

	AddInfo(FString::Printf(TEXT("TIMERS: Outpost %.0f with the tree, %.0f without; Bulwark %.0f with; held %d, then "
								 "resolved %d"),
							OutpostWith, OutpostWithout, BulwarkWith, bHeld, bResolved));
	TestEqual(TEXT("an Outpost's timer: 30 and 15"), OutpostWith, 45.0f, 0.001f);
	TestEqual(TEXT("a Bulwark's: 2 and 10"), BulwarkWith, 12.0f, 0.001f);
	TestTrue(TEXT("a timer with the tree's days left does not resolve"), bHeld);
	TestEqual(TEXT("without the tree the Outpost's timer has its own 29"), OutpostWithout, 29.0f, 0.001f);
	TestTrue(TEXT("and the Bulwark's resolves the next day"), bResolved);
	return true;
}

/**
 * Martial Law adds 5 days to the timers on a city below a tenth of its defence, and none above. Prints "MARTIAL".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityMartialLawTest,
	"Cataclysm.EmpireTreeCity.MartialLawAddsDaysBelowATenthOfDefence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityMartialLawTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmEmpireRun* Run = QuietRun(2741, BonusesOf(Holding(MartialLaw, 1)));
	FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
	{
		return false;
	}
	Place(*Run, 9301, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 20.0f);
	Bulwark->Defence = Bulwark->MaxDefence * 0.5f;
	const float AtHalf = Run->Clock->DaysUntilResolveFor(9301);
	Bulwark->Defence = Bulwark->MaxDefence * 0.05f;
	const float AtAFifth = Run->Clock->DaysUntilResolveFor(9301);
	AddInfo(FString::Printf(TEXT("MARTIAL: at half %.0f days, at 5%% %.0f"), AtHalf, AtAFifth));
	TestEqual(TEXT("at half its defence, the timer's own 20"), AtHalf, 20.0f, 0.001f);
	TestEqual(TEXT("below a tenth, 5 more"), AtAFifth, 25.0f, 0.001f);
	return true;
}

/**
 * The Flood Barrier adds 20 days to every gap between surges and not before the first wave, as the simulation adds
 * `surge_bonus_days` to the gap it records; and a respec takes them off the next surge at once. Prints "GAP".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityGapTest,
	"Cataclysm.EmpireTreeCity.TheFloodBarrierLengthensEveryGapAfterTheFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityGapTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmSurgeScheduler* Surges = NewObject<UCataclysmSurgeScheduler>();
	Surges->GapBonusDays = 20.0f;
	const bool bFirstDue = Surges->IsDue(0);
	Surges->RecordSurge(0);
	const float After = Surges->DaysUntilNextSurge(0);
	const float Gap = Surges->NextSurgeDay;
	Surges->GapBonusDays = 0.0f;
	const float Respecced = Surges->DaysUntilNextSurge(0);
	AddInfo(FString::Printf(TEXT("GAP: first due %d; after the first, %.0f days (gap %.0f); without it %.0f"), bFirstDue,
							After, Gap, Respecced));
	TestTrue(TEXT("the first wave is due on day 0 whatever the tree"), bFirstDue);
	TestEqual(TEXT("the next is the gap and 20"), After, Gap + 20.0f, 0.001f);
	TestEqual(TEXT("and the gap alone once the tree is gone"), Respecced, Gap, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// City upgrades, a retake, and the Royal Engineer
// ---------------------------------------------------------------------------

/**
 * Under the tree a city upgrade that raises maximum defence keeps a full city full, a retake gives half the raised
 * maximum, and The Royal Engineer's Blessing counts every standing city holding an upgrade that raises maximum defence
 * or resists defence loss, and not one that only restores (S5). Prints "UPGRADES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityUpgradesTest,
	"Cataclysm.EmpireTreeCity.UpgradesRetakesAndTheRoyalEngineerReadTheRaisedMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityUpgradesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->TreeBonuses = BonusesOf(Holding(Foundation, 10));
	Map->Build();

	// A FULL OUTPOST BUYS +20% MAXIMUM DEFENCE: still full.
	FCataclysmCity* Outpost = nullptr;
	TArray<FCataclysmCity*> Bulwarks;
	for (FCataclysmCity& City : Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Outpost && Outpost == nullptr)
		{
			Outpost = &City;
		}
		if (City.Tier == ECataclysmCityTier::Bulwark && Bulwarks.Num() < 3)
		{
			Bulwarks.Add(&City);
		}
	}
	if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost) || !TestEqual(TEXT("set-up: three Bulwarks"), Bulwarks.Num(), 3))
	{
		return false;
	}
	FCataclysmCityUpgrade Raise;
	Raise.Effect = ECataclysmCityUpgradeEffect::MaxDefence;
	Raise.Value = 0.2f;
	Map->AddUpgrade(Outpost->CityId, Raise);
	const float OutpostMax = Map->EffectiveMaxDefence(*Outpost);
	const float OutpostHeld = Map->DefenceOf(*Outpost);

	// A RETAKE: half the raised maximum.
	Map->Fall(Bulwarks[0]->CityId);
	Map->Retake(Bulwarks[0]->CityId);
	const float Retaken = Bulwarks[0]->Defence;

	// THE ROYAL ENGINEER: the Outpost's raise and one Bulwark's resistance count; another Bulwark's restore does not.
	FCataclysmCityUpgrade Resist;
	Resist.Effect = ECataclysmCityUpgradeEffect::ResistDefenceLoss;
	Resist.Value = 0.25f;
	FCataclysmCityUpgrade Restore;
	Restore.Effect = ECataclysmCityUpgradeEffect::RestoreDefence;
	Restore.Value = 0.5f;
	Map->AddUpgrade(Bulwarks[1]->CityId, Resist);
	Map->AddUpgrade(Bulwarks[2]->CityId, Restore);
	FCataclysmPassiveAllocation WithEngineer = Holding(Foundation, 10);
	WithEngineer.Add(RoyalEngineer, 1);
	Map->TreeBonuses = BonusesOf(WithEngineer);
	const int32 Defended = Map->CitiesWithADefensiveUpgrade();
	const float PillarFactor = Map->EffectiveMaxDefence(*Map->Find(Map->PillarId)) / UCataclysmEmpireMap::PillarMaxDefence;

	AddInfo(FString::Printf(TEXT("UPGRADES: Outpost %.0f of %.0f after +20%%; retaken %.0f; defended cities %d, Pillar x%.2f"),
							OutpostHeld, OutpostMax, Retaken, Defended, PillarFactor));
	TestEqual(TEXT("the Outpost's maximum: 1200 base x1.5"), OutpostMax, 1800.0f, 0.01f);
	TestEqual(TEXT("and it is still full"), OutpostHeld, 1800.0f, 0.01f);
	TestEqual(TEXT("a retaken Bulwark: half of 3000 x1.5"), Retaken, 2250.0f, 0.01f);
	TestEqual(TEXT("two cities hold a defensive upgrade"), Defended, 2);
	TestEqual(TEXT("so every city has Foundation's +50% and 2 x 2%"), PillarFactor, 1.54f, 0.0001f);
	return true;
}

// ---------------------------------------------------------------------------
// Saving, buying and respec
// ---------------------------------------------------------------------------

/**
 * Nothing of the tree is saved, so a load counts nothing twice: a run begun with Foundation in full saves its Outpost's
 * `MaxDefence` as 1000, its timers without the tree's days and no word of the bonuses; read back and handed the same
 * tree, it reads the same maximum, holding and timer as the run that was saved. Prints "SAVED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCitySavedTest,
	"Cataclysm.EmpireTreeCity.ASavedRunHoldsTheBaseAndLoadsWithNothingCountedTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCitySavedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	FCataclysmPassiveAllocation Tree = Holding(Foundation, 10);
	Tree.Add(StrategicReserve, 10);
	const FCataclysmEmpireTreeBonuses Bonuses = BonusesOf(Tree);
	UCataclysmEmpireRun* Live = QuietRun(2741, Bonuses);
	const FCataclysmCity* LiveOutpost = FirstOf(*Live, ECataclysmCityTier::Outpost);
	if (!TestNotNull(TEXT("set-up: an Outpost"), LiveOutpost))
	{
		return false;
	}
	const int32 OutpostId = LiveOutpost->CityId;
	Place(*Live, 9401, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *LiveOutpost, 30.0f);

	FString Json;
	FString Why;
	UCataclysmRunSave* Record = SavedAndReadBack(*Live, Json, Why);
	if (!TestNotNull(*FString::Printf(TEXT("set-up: the run was saved and read back (%s)"), *Why), Record))
	{
		return false;
	}
	const FCataclysmCity* SavedOutpost = Record->Cities.FindByPredicate(
		[OutpostId](const FCataclysmCity& City) { return City.CityId == OutpostId; });

	UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
	const bool bLoaded = FCataclysmSaveApply::RunInto(*Record, *Loaded, Why);
	Loaded->SetTreeBonuses(Bonuses);
	const FCataclysmCity* LoadedOutpost = bLoaded ? Loaded->Map->Find(OutpostId) : nullptr;

	AddInfo(FString::Printf(TEXT("SAVED: the record's Outpost %.0f of base %.0f; live %.0f of %.0f, timer %.0f; loaded %.0f "
								 "of %.0f, timer %.0f"),
							SavedOutpost ? SavedOutpost->Defence : -1.0f, SavedOutpost ? SavedOutpost->MaxDefence : -1.0f,
							Live->Map->DefenceOf(*LiveOutpost), Live->Map->EffectiveMaxDefence(*LiveOutpost),
							Live->Clock->DaysUntilResolveFor(9401),
							LoadedOutpost ? Loaded->Map->DefenceOf(*LoadedOutpost) : -1.0f,
							LoadedOutpost ? Loaded->Map->EffectiveMaxDefence(*LoadedOutpost) : -1.0f,
							bLoaded ? Loaded->Clock->DaysUntilResolveFor(9401) : -1.0f));
	TestTrue(TEXT("the record holds the base maximum, 1000"),
			 SavedOutpost != nullptr && SavedOutpost->MaxDefence == UCataclysmEmpireMap::OutpostMaxDefence);
	TestFalse(TEXT("and no word of the bonuses"), Json.Contains(TEXT("MaxDefenceAll")) || Json.Contains(TEXT("TreeBonuses")));
	if (!TestTrue(*FString::Printf(TEXT("the record went back into a run (%s)"), *Why), bLoaded && LoadedOutpost != nullptr))
	{
		return false;
	}
	TestEqual(TEXT("loaded, the same maximum"), Loaded->Map->EffectiveMaxDefence(*LoadedOutpost),
			  Live->Map->EffectiveMaxDefence(*LiveOutpost), 0.01f);
	TestEqual(TEXT("the same holding"), Loaded->Map->DefenceOf(*LoadedOutpost), Live->Map->DefenceOf(*LiveOutpost), 0.01f);
	TestEqual(TEXT("and the same timer, the tree's 10 on its own 30, not twice"), Loaded->Clock->DaysUntilResolveFor(9401),
			  Live->Clock->DaysUntilResolveFor(9401), 0.001f);
	return true;
}

/**
 * Through the game instance, as the tree's screen buys: a point of Foundation acts on the run being played at once, an
 * Outpost's maximum 1000 to 1050, and a respec takes it away at once. And the bonuses the game instance reads for a world
 * are its player's tree, so a run handed them before `Begin` builds its cities full. Prints "THROUGH".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCityThroughTest,
	"Cataclysm.EmpireTreeCity.ABuyActsAtOnceAndARespecTakesItAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCityThroughTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCityTest;

	FCityTreeScene Scene = MakeScene();
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.Account();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()) || !TestNotNull(TEXT("set-up: its account"), Account))
	{
		return false;
	}
	Scene.Instance->BeginEmpireRun(/*Seed*/ 2741, ECataclysmSurgeMode::Static, /*LethalityRung*/ 0, /*DifficultyTier*/ 1);
	UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(Scene.World, false);
	const FCataclysmCity* Outpost = Run != nullptr ? FirstOf(*Run, ECataclysmCityTier::Outpost) : nullptr;
	if (!TestNotNull(TEXT("set-up: a run with an Outpost"), Outpost))
	{
		return false;
	}
	const int32 OutpostId = Outpost->CityId;
	Account->EmpireUpgradePoints = 1.0;
	// FOUNDATION OPENS AT 2 POINTS OF URBAN PLANNING, its edge in the tree file; Urban Planning raises Outposts' people
	// and not their defence, so what is measured below is Foundation's alone.
	Account->EmpireTree.Add(UrbanPlanning, 2);

	const float Before = Run->Map->EffectiveMaxDefence(*Run->Map->Find(OutpostId));
	FString Reason;
	const bool bBought = UCataclysmGameInstance::BuyEmpireTreePoint(Scene.World, Foundation, Reason);
	const float Bought = Run->Map->EffectiveMaxDefence(*Run->Map->Find(OutpostId));
	int32 Returned = 0;
	int32 Days = 0;
	const bool bRespecced = UCataclysmGameInstance::RespecEmpireTree(Scene.World, Returned, Days, Reason);
	const float Respecced = Run->Map->EffectiveMaxDefence(*Run->Map->Find(OutpostId));

	// AND A RUN HANDED THE PLAYER'S TREE BEFORE IT BEGINS, as `BeginACharactersEmpireRun` hands it.
	Account->EmpireTree.Add(Foundation, 10);
	UCataclysmEmpireRun* Fresh = NewObject<UCataclysmEmpireRun>();
	Fresh->SetTreeBonuses(UCataclysmGameInstance::EmpireTreeBonusesFor(Scene.World));
	Fresh->Begin(2741);
	const FCataclysmCity* FreshOutpost = FirstOf(*Fresh, ECataclysmCityTier::Outpost);

	AddInfo(FString::Printf(TEXT("THROUGH: %.0f, bought %s to %.0f, respecced %s to %.0f; a run begun with the player's tree "
								 "holds %.0f"),
							Before, bBought ? TEXT("yes") : TEXT("no"), Bought, bRespecced ? TEXT("yes") : TEXT("no"),
							Respecced, FreshOutpost ? FreshOutpost->Defence : -1.0f));
	TestEqual(TEXT("before: 1000"), Before, 1000.0f, 0.01f);
	TestTrue(TEXT("the point was bought"), bBought);
	TestEqual(TEXT("and acts at once: 1050"), Bought, 1050.0f, 0.01f);
	TestTrue(TEXT("the respec was made"), bRespecced);
	TestEqual(TEXT("and takes it away at once: 1000"), Respecced, 1000.0f, 0.01f);
	TestTrue(TEXT("a run begun with the player's tree holds 1500 in an Outpost"),
			 FreshOutpost != nullptr && FMath::IsNearlyEqual(FreshOutpost->Defence, 1500.0f, 0.01f));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
