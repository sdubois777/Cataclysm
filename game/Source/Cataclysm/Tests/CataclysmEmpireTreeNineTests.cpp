// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Items/CataclysmDropRoll.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmCityUpgradePurchase.h"
#include "Player/CataclysmDungeonBribe.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The empire upgrade tree's slice 9: the five nodes the coordinating session ruled under the owner's delegation on
 * 2026-11-21, and its V1 to V20 of 2026-11-22. Issue #2741.
 *
 *   Civic Pride        each of the four population restores 5% larger a point, held at the city's maximum (V11)
 *   Reinvestment       1% more empire points a clear pays per 25,000 tier 1 gold spent on city upgrades across runs,
 *                      capped at 30% on its own and added to Veteran's Edge's (V2 to V5, V20)
 *   Economic Bailout   a city whose defence reaches 0 is saved once a run when the balance pays 50,000, 100,000 or
 *                      150,000 tier 1 gold, keeping half its maximum defence; never the Pillar (V6 to V10)
 *   Bribery            once a surge, 20,000 tier 1 gold resets one resolving dungeon's timer to its start (V12 to V14)
 *   Scout Parties      10% a point that a clear discovers a bonus dungeon on the same city, which threatens nothing
 *                      and moves no pressure (V15 to V19)
 *
 * FIVE NEW GROUPS, each selecting these and nothing else: `Cataclysm.EmpireTreeRestores.` (part a),
 * `Cataclysm.EmpireTreeBailout.` (part b), `Cataclysm.EmpireTreeBribery.` and `Cataclysm.EmpireTreeScouts.` (part c),
 * and `Cataclysm.EmpireTreeNineSaves.`, the three records' steps.
 *
 * THE ACCOUNT SLOT IS THE TEST'S OWN, as in `Cataclysm.EmpireTreeCredit.`: each scene deletes it first, and nothing here
 * begins the save writer, so nothing is written.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeNineTest
{
	// The tree file's identifiers, by the node names the table gives them.
	const FName CivicPride(TEXT("basic_arch_civic_pride"));	// +5% a point, 10 points
	const FName Reinvestment(TEXT("keystone_00000044"));	// 1% a 25,000, capped at 30%, 1 point
	const FName Bribery(TEXT("keystone_00000042"));			// 20,000 tier 1 gold, once a surge, 1 point

	/** The upgrade the purchases buy: built, and the sheet's first row, 10,000 gold at tier 1. */
	const TCHAR* const Row = TEXT("Architect_Increase_max_defense_by_20");

	/** A second built upgrade at the same price, for a second purchase on the same city. */
	const TCHAR* const SecondRow = TEXT("Architect_Increase_max_population_by_20");

	/** A run begun with these bonuses, its first day spent and its board emptied, as the earlier slices' tests leave
	 *  one. The bonuses are set again after `Begin`, so the map holds them whichever way `Begin` treats them. */
	UCataclysmEmpireRun* QuietRun(int32 Seed, const FCataclysmEmpireTreeBonuses& Bonuses = FCataclysmEmpireTreeBonuses())
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->SetTreeBonuses(Bonuses);
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		Run->SetTreeBonuses(Bonuses);
		return Run;
	}

	/** The standing cities of a tier, in id order. */
	TArray<FCataclysmCity*> AllOf(UCataclysmEmpireRun& Run, ECataclysmCityTier Tier)
	{
		TArray<FCataclysmCity*> Out;
		for (FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == Tier && !City.bFallen)
			{
				Out.Add(&City);
			}
		}
		return Out;
	}

	/** Puts a dungeon on a city with a timer, as a surge would; a bonus one when asked. */
	void Place(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type, const FCataclysmCity& City,
			   float ResolveDays, bool bBonus = false)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = ECataclysmDungeonSubType::None;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 20;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.SpawnedDay = Run.Day();
		Dungeon.DefenceDamage = 100.0f;
		Dungeon.PopulationDamage = 400.0f;
		Dungeon.bBonus = bBonus;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/** The clock's timer of a dungeon, or null. */
	FCataclysmDungeonTimer* TimerOf(UCataclysmEmpireRun& Run, int32 DungeonId)
	{
		return Run.Clock->Timers.FindByPredicate(
			[DungeonId](const FCataclysmDungeonTimer& Timer) { return Timer.DungeonId == DungeonId; });
	}

	/** A dungeon on this city, not on the board, for asking the state of. */
	FCataclysmDungeon DungeonOn(int32 CityId)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = 9901;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.CityId = CityId;
		Dungeon.CityTier = ECataclysmCityTier::Outpost;
		Dungeon.Floors = 10;
		Dungeon.Bosses = 1;
		return Dungeon;
	}

	/** An upgrade of one effect, as `UCataclysmCityUpgradeMapping::Make` fills one in. */
	FCataclysmCityUpgrade AnUpgrade(const TCHAR* RowName, ECataclysmCityUpgradeEffect Effect, float Value)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = FName(RowName);
		Upgrade.Effect = Effect;
		Upgrade.Value = Value;
		return Upgrade;
	}

	/** The four population restores, as shares of the maximum each leaves a city at. */
	struct FFourRestores
	{
		float Retake = 0.0f;
		float RetakeDefence = 0.0f;
		float Upgrade = 0.0f;
		float Guild = 0.0f;
		float Recovery = 0.0f;
		bool bSetUp = false;
	};

	/**
	 * Each of the four restores, on its own city of a run begun with these bonuses: a retake of a fallen Outpost; the
	 * one-time "Restore city's population by 50%" from 20%; The Mason's Guild's 15% on the Bulwark with the lowest
	 * defence, from half its people; and the 20-day recovery's 5% on a Sanctuary, from half, through a day of the run.
	 */
	FFourRestores RestoresWith(const FCataclysmEmpireTreeBonuses& Bonuses)
	{
		FFourRestores Out;
		UCataclysmEmpireRun* Run = QuietRun(2741, Bonuses);
		const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
		const TArray<FCataclysmCity*> Bulwarks = AllOf(*Run, ECataclysmCityTier::Bulwark);
		const TArray<FCataclysmCity*> Sanctuaries = AllOf(*Run, ECataclysmCityTier::Sanctuary);
		if (Outposts.Num() < 2 || Bulwarks.Num() < 2 || Sanctuaries.Num() < 1)
		{
			return Out;
		}
		Out.bSetUp = true;
		UCataclysmEmpireMap& Map = *Run->Map;
		const int32 Retaken = Outposts[0]->CityId;
		const int32 Restored = Outposts[1]->CityId;
		const int32 Guilded = Bulwarks[0]->CityId;
		const int32 Recovering = Sanctuaries[0]->CityId;

		// A RETAKE.
		Map.FindMutable(Retaken)->bFallen = true;
		Map.Retake(Retaken);
		const FCataclysmCity& AfterRetake = *Map.Find(Retaken);
		Out.Retake = Map.PopulationOf(AfterRetake) / Map.EffectiveMaxPopulation(AfterRetake);
		Out.RetakeDefence = Map.DefenceOf(AfterRetake) / Map.EffectiveMaxDefence(AfterRetake);

		// THE ONE-TIME UPGRADE, 50%, from a fifth of its people.
		FCataclysmCity& Upgraded = *Map.FindMutable(Restored);
		Upgraded.Population = 0.2f * Map.EffectiveMaxPopulation(Upgraded);
		Map.AddUpgrade(Restored, AnUpgrade(TEXT("Architect_Restore_population_by_50"),
										   ECataclysmCityUpgradeEffect::RestorePopulation, 0.5f));
		Out.Upgrade = Map.PopulationOf(*Map.Find(Restored)) / Map.EffectiveMaxPopulation(*Map.Find(Restored));

		// THE MASON'S GUILD on day 20, the Bulwark the lowest share of its defence of every Outpost and Bulwark.
		FCataclysmCity& Guild = *Map.FindMutable(Guilded);
		Guild.Defence = 0.1f * Map.EffectiveMaxDefence(Guild);
		Guild.Population = 0.5f * Map.EffectiveMaxPopulation(Guild);
		Run->RunTreeIntervals(20);
		Out.Guild = Map.PopulationOf(*Map.Find(Guilded)) / Map.EffectiveMaxPopulation(*Map.Find(Guilded));

		// THE 20-DAY RECOVERY, due today, through a day of the run. A dungeon with a long timer on another city keeps
		// the board from being empty, and the next surge is put far off, so the day does nothing else to this city.
		FCataclysmCity& Recovery = *Map.FindMutable(Recovering);
		Recovery.Population = 0.5f * Map.EffectiveMaxPopulation(Recovery);
		FCataclysmCityUpgrade Every = AnUpgrade(TEXT("Architect_Recover_population_every_20_days"),
												ECataclysmCityUpgradeEffect::RecoverPopulationEvery, 0.05f);
		Every.IntervalDays = 20.0f;
		Every.NextTriggerDay = Run->Day();
		Every.BoughtDay = Run->Day();
		Recovery.Upgrades.Add(Every);
		Place(*Run, 9400, ECataclysmDungeonType::Basic, *Bulwarks[1], 60.0f);
		Run->Surges->NextSurgeDay = static_cast<float>(Run->Day() + 100);
		Run->AdvanceDay();
		Out.Recovery = Map.PopulationOf(*Map.Find(Recovering)) / Map.EffectiveMaxPopulation(*Map.Find(Recovering));
		return Out;
	}

	/** A world with this project's game instance, a possessed player of one partition, and a run on the instance. */
	struct FNineScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State && Run && Run->Map; }

		/** The mode's account record the scene's character banks into. */
		UCataclysmAccountSave* Account() const { return Instance ? Instance->AccountFor(Partition) : nullptr; }
	};

	/** Waits for any write to the slot to finish, then deletes it when it exists. */
	void DeleteSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UCataclysmSaveWriter::UserIndex);
		}
	}

	/** The scene of `Cataclysm.EmpireTreeCredit.`: a Standard partition, offline, Solo Self-Found or not, its mode's
	 *  account slot emptied first, and a run begun on the price tests' seed, at difficulty tier 1, on the instance. */
	FNineScene MakeScene(bool bSoloSelfFound = false)
	{
		FNineScene Out;
		Out.Partition.Lethality = ECataclysmLethality::Standard;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

		FCataclysmCharacterPartition Mode = Out.Partition;
		Mode.bSoloSelfFound = false;
		DeleteSlot(UCataclysmGameInstance::AccountSlotFor(Mode));

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
			Out.State->SetIdentity(TEXT("Investor"), Out.Partition);
		}

		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2742, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		return Out;
	}

	/** Destroys a scene's world. */
	void EndScene(FNineScene& Scene)
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			Scene.World = nullptr;
		}
	}

	/** The run's answer as a number a TestEqual prints. */
	int32 Answer(ECataclysmCityUpgradeResult Result) { return static_cast<int32>(Result); }

	/** A record of one class, read through the whole chain as a load reads it; null, with an error, when it will not. */
	template <typename RecordType>
	const RecordType* ReadFixture(FAutomationTestBase& Test, const TCHAR* Name, ECataclysmSaveLoadResult& OutResult)
	{
		FString Text;
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Name, Text, Reason))
		{
			Test.AddError(Reason);
			return nullptr;
		}
		FString Message;
		OutResult = ECataclysmSaveLoadResult::NotValidJson;
		const RecordType* Read = Cast<RecordType>(FCataclysmSaveStorage::FromJson(
			Text, RecordType::StaticClass(), GetTransientPackage(), OutResult, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(OutResult), *Message));
		}
		return Read;
	}
}

// ---------------------------------------------------------------------------
// Part a: Civic Pride and Reinvestment
// ---------------------------------------------------------------------------

/**
 * The two add up through the generated tables: Civic Pride at 10 points 0.50 on the city bonuses; Reinvestment at 1
 * point 0.01 a step with its own 0.30 cap on the player bonuses. Its share of the award is whole steps of 25,000 tier 1
 * gold: 24,999 spent gives 0, 25,000 gives 0.01, 800,000 gives 0.30 and not 0.32. CONTROL: no node gives 0 at 800,000.
 * Prints "NINE TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeRestoresTableTest,
	"Cataclysm.EmpireTreeRestores.TheTwoNodesAddUpFromTheirRowsAndTheSpendingShareIsCapped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeRestoresTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(CivicPride, 10);
	Tree.Add(Reinvestment, 1);
	const FCataclysmEmpireTreeBonuses City = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreePlayerBonuses Player = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreePlayerBonuses None;
	const float Under = UCataclysmEmpireTree::EmpirePointsFromSpendingFor(Player, 24999);
	const float One = UCataclysmEmpireTree::EmpirePointsFromSpendingFor(Player, 25000);
	const float Capped = UCataclysmEmpireTree::EmpirePointsFromSpendingFor(Player, 800000);
	const float NoNode = UCataclysmEmpireTree::EmpirePointsFromSpendingFor(None, 800000);
	AddInfo(FString::Printf(TEXT("NINE TABLE: civic %.3f; reinvestment %.3f a step, cap %.3f; at 24,999 %.3f, 25,000 "
								 "%.3f, 800,000 %.3f, no node %.3f"),
							City.PopulationRestoreIncrease, Player.EmpirePointsPerSpendingStep,
							Player.EmpirePointsSpendingCap, Under, One, Capped, NoNode));

	TestEqual(TEXT("Civic Pride at 10 points: 0.50"), City.PopulationRestoreIncrease, 0.50f, 1.0e-5f);
	TestEqual(TEXT("Reinvestment: 0.01 a step"), Player.EmpirePointsPerSpendingStep, 0.01f, 1.0e-6f);
	TestEqual(TEXT("with its own cap, 0.30"), Player.EmpirePointsSpendingCap, 0.30f, 1.0e-6f);
	TestEqual(TEXT("24,999 spent: no whole step"), Under, 0.0f, 1.0e-6f);
	TestEqual(TEXT("25,000 spent: one step, 0.01"), One, 0.01f, 1.0e-6f);
	TestEqual(TEXT("800,000 spent: 32 steps held at 0.30"), Capped, 0.30f, 1.0e-6f);
	TestEqual(TEXT("CONTROL: no node, nothing"), NoNode, 0.0f, 1.0e-6f);
	return true;
}

/**
 * Civic Pride at 10 points makes each of the four population restores 50% larger, held at the city's maximum (V11): a
 * retake with The Architect's Legacy's 0.75 leaves 1.00 of the maximum population and not 1.125, and 0.75 of the maximum
 * defence, which it does not raise; the one-time 50% upgrade from 0.20 leaves 0.95; The Mason's Guild's 15% from 0.50
 * leaves 0.725; the 20-day 5% from 0.50 leaves 0.575. CONTROL: without it, 0.75, 0.75, 0.70, 0.65 and 0.55. Prints
 * "CIVIC PRIDE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeRestoresCivicPrideTest,
	"Cataclysm.EmpireTreeRestores.CivicPrideMakesEachPopulationRestoreLargerToTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeRestoresCivicPrideTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	FCataclysmEmpireTreeBonuses Plain;
	Plain.RetakeFraction = 0.75f;
	Plain.LowestCityDefenceRestored = 0.20f;
	Plain.LowestCityPopulationRestored = 0.15f;
	FCataclysmEmpireTreeBonuses Civic = Plain;
	Civic.PopulationRestoreIncrease = 0.50f;
	const FFourRestores With = RestoresWith(Civic);
	const FFourRestores Without = RestoresWith(Plain);
	if (!TestTrue(TEXT("set-up: two Outposts, two Bulwarks and a Sanctuary"), With.bSetUp && Without.bSetUp))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("CIVIC PRIDE: retake %.4f (defence %.4f), upgrade %.4f, guild %.4f, recovery %.4f; "
								 "without: %.4f (%.4f), %.4f, %.4f, %.4f"),
							With.Retake, With.RetakeDefence, With.Upgrade, With.Guild, With.Recovery, Without.Retake,
							Without.RetakeDefence, Without.Upgrade, Without.Guild, Without.Recovery));

	TestEqual(TEXT("a retake at 0.75 x 1.5 is held at the maximum"), With.Retake, 1.0f, 1.0e-4f);
	TestEqual(TEXT("and its defence is not raised"), With.RetakeDefence, 0.75f, 1.0e-4f);
	TestEqual(TEXT("the one-time 50% upgrade gives 75%: 0.20 to 0.95"), With.Upgrade, 0.95f, 1.0e-4f);
	TestEqual(TEXT("The Mason's Guild's 15% gives 22.5%: 0.50 to 0.725"), With.Guild, 0.725f, 1.0e-4f);
	TestEqual(TEXT("the 20-day 5% gives 7.5%: 0.50 to 0.575"), With.Recovery, 0.575f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: a retake at 0.75"), Without.Retake, 0.75f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: the upgrade, 0.70"), Without.Upgrade, 0.70f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: the guild, 0.65"), Without.Guild, 0.65f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: the recovery, 0.55"), Without.Recovery, 0.55f, 1.0e-4f);
	return true;
}

/**
 * Reinvestment's count (V2, V4, V5, V20): ONE 10,000 UPGRADE ADDS EXACTLY 10,000 AT TWO TIERS, the coordinating
 * session's test for V20. At difficulty tier 2, with 20,000 already counted and the node held, a purchase accepted adds
 * its price charged, 30,000, over tier 2's gold scale, 3, unrounded: 10,000; and the run's share is read again at once,
 * 0.01 at 30,000. A purchase the run refuses, a second
 * on the city building the first, adds nothing. A Solo Self-Found character at tier 1 counts its own 10,000, and its
 * mode's account counts nothing. Prints "REINVESTMENT COUNT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeRestoresCountTest,
	"Cataclysm.EmpireTreeRestores.APurchaseCountsItsPriceInTierOneGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeRestoresCountTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	FNineScene Scene = MakeScene();
	FNineScene Own = MakeScene(/*bSoloSelfFound=*/true);
	ON_SCOPE_EXIT
	{
		EndScene(Scene);
		EndScene(Own);
	};
	UCataclysmAccountSave* Account = Scene.IsUsable() ? Scene.Account() : nullptr;
	if (!TestTrue(TEXT("set-up: both scenes were built"), Scene.IsUsable() && Own.IsUsable())
		|| !TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	const FName RowName(Row);
	Scene.Run->DifficultyTier = 2;
	Account->EmpireTree.Add(Reinvestment, 1);
	Account->Gold = 1000000;
	Account->GoldSpentOnCityUpgrades = 20000;
	UCataclysmGameInstance::RefreshEmpireTreeBonuses(Scene.World);
	const float ShareBefore = Scene.Run->TreeBonuses.EmpirePointsFromSpending;

	const int64 Price = UCataclysmCityUpgradePurchase::PriceIn(Scene.World, Scene.Run, RowName, 0);
	const double Scale = UCataclysmDropRoll::GoldScaleFor(2);
	const ECataclysmCityUpgradeResult Bought = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	const double Counted = Account->GoldSpentOnCityUpgrades - 20000.0;
	const float ShareAfter = Scene.Run->TreeBonuses.EmpirePointsFromSpending;
	const ECataclysmCityUpgradeResult Refused =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(SecondRow));
	const double AfterTheRefusal = Account->GoldSpentOnCityUpgrades;

	Own.State->SetOwnGold(1000000);
	const ECataclysmCityUpgradeResult OwnBought = UCataclysmCityUpgradePurchase::Buy(Own.World, Own.Run, 0, RowName);
	FCataclysmCharacterPartition Mode = Own.Partition;
	Mode.bSoloSelfFound = false;
	const UCataclysmAccountSave* ModeAccount = Own.Instance->AccountFor(Mode);
	AddInfo(FString::Printf(TEXT("REINVESTMENT COUNT: tier 2 price %lld at scale %.4f, %s, counted %.4f, share %.3f to "
								 "%.3f; refused %s, count %.4f; own %s, own count %.4f, the mode's account %.4f"),
							Price, Scale, *UCataclysmCityUpgradeRules::ResultText(Bought), Counted, ShareBefore, ShareAfter,
							*UCataclysmCityUpgradeRules::ResultText(Refused), AfterTheRefusal,
							*UCataclysmCityUpgradeRules::ResultText(OwnBought), Own.State->GetOwnGoldSpentOnCityUpgrades(),
							ModeAccount ? ModeAccount->GoldSpentOnCityUpgrades : -1.0));

	TestEqual(TEXT("the purchase is accepted"), Answer(Bought), Answer(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("it counts its price over the tier's gold scale, unrounded"), Counted,
			  static_cast<double>(Price) / FMath::Max(1.0e-6, Scale));
	TestEqual(TEXT("which is exactly 10,000 at tier 2: 30,000 over 3"), Counted, 10000.0);
	TestEqual(TEXT("CONTROL: 20,000 counted is no whole step"), ShareBefore, 0.0f, 1.0e-6f);
	TestEqual(TEXT("and the run reads 0.01 at 30,000, at once"), ShareAfter, 0.01f, 1.0e-6f);
	TestEqual(TEXT("a refused purchase is refused"), Answer(Refused), Answer(ECataclysmCityUpgradeResult::BuildUnderWay));
	TestEqual(TEXT("and counts nothing"), AfterTheRefusal - 20000.0, Counted);
	TestEqual(TEXT("a Solo Self-Found character's purchase is accepted"), Answer(OwnBought),
			  Answer(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("and counts in its own record exactly 10,000 at tier 1, as at tier 2"),
			  Own.State->GetOwnGoldSpentOnCityUpgrades(), 10000.0);
	TestTrue(TEXT("the mode's account counts nothing"),
			 ModeAccount == nullptr || ModeAccount->GoldSpentOnCityUpgrades == 0.0);
	return true;
}

/**
 * Reinvestment's share is added to Veteran's Edge's on a clear (V3): with 0.30 of each, a Basic clear pays 1.6 times
 * `EmpirePointsFor`'s base. CONTROL: with Veteran's Edge's 0.30 alone, 1.3 times. Prints "REINVESTMENT AWARD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeRestoresAwardTest,
	"Cataclysm.EmpireTreeRestores.ReinvestmentAddsToVeteransEdgeOnAClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeRestoresAwardTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	FCataclysmEmpireTreeBonuses Edge;
	Edge.EmpirePointsIncrease = 0.30f;
	FCataclysmEmpireTreeBonuses Both = Edge;
	Both.EmpirePointsFromSpending = 0.30f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Both);
	UCataclysmEmpireRun* Control = QuietRun(2741, Edge);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const TArray<FCataclysmCity*> ControlOutposts = AllOf(*Control, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: an Outpost in each run"), Outposts.Num() > 0 && ControlOutposts.Num() > 0))
	{
		return false;
	}
	Place(*Run, 9410, ECataclysmDungeonType::Basic, *Outposts[0], 30.0f);
	Place(*Control, 9410, ECataclysmDungeonType::Basic, *ControlOutposts[0], 30.0f);
	const double Base = UCataclysmEmpireRun::EmpirePointsFor(ECataclysmDungeonType::Basic,
		static_cast<double>(Run->Map->TotalPopulation()), static_cast<double>(Run->Map->TotalMaxPopulation()));
	const double ControlBase = UCataclysmEmpireRun::EmpirePointsFor(ECataclysmDungeonType::Basic,
		static_cast<double>(Control->Map->TotalPopulation()), static_cast<double>(Control->Map->TotalMaxPopulation()));
	const bool bCleared = Run->ClearDungeon(9410);
	const bool bControlCleared = Control->ClearDungeon(9410);
	AddInfo(FString::Printf(TEXT("REINVESTMENT AWARD: base %.4f, awarded %.4f; control base %.4f, awarded %.4f"), Base,
							Run->LastEmpirePointsAwarded, ControlBase, Control->LastEmpirePointsAwarded));

	TestTrue(TEXT("set-up: both clears happened, on a base above nought"), bCleared && bControlCleared && Base > 0.0);
	TestEqual(TEXT("Veteran's Edge's 0.30 and Reinvestment's 0.30: 1.6 times the base"), Run->LastEmpirePointsAwarded,
			  Base * 1.6, 1.0e-6);
	TestEqual(TEXT("CONTROL: Veteran's Edge's alone, 1.3 times"), Control->LastEmpirePointsAwarded, ControlBase * 1.3,
			  1.0e-6);
	return true;
}

// ---------------------------------------------------------------------------
// Part b: Economic Bailout
// ---------------------------------------------------------------------------

/**
 * A dungeon's damage that takes an Outpost's defence to 0 asks the bailout once, which pays: the city is saved, marked,
 * at half its maximum defence (V8) with its population as it was, and the day's report lists it. A second such hit the
 * same run makes it fall, without asking again ("Once per city"). Prints "BAILOUT SAVED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeBailoutSavedTest,
	"Cataclysm.EmpireTreeBailout.ACityTakenToNothingIsSavedOnceAtHalfItsDefence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeBailoutSavedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	FCataclysmEmpireTreeBonuses Bailout;
	Bailout.CityBailoutPoints = 1;
	UCataclysmEmpireRun* Run = QuietRun(2741, Bailout);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: an Outpost"), Outposts.Num() > 0))
	{
		return false;
	}
	const int32 CityId = Outposts[0]->CityId;
	int32 Asked = 0;
	ECataclysmCityTier AskedFor = ECataclysmCityTier::Pillar;
	Run->PayTheBailout = [&Asked, &AskedFor](UCataclysmEmpireRun&, int32, ECataclysmCityTier Tier)
	{
		++Asked;
		AskedFor = Tier;
		return true;
	};
	UCataclysmEmpireMap& Map = *Run->Map;
	const float People = Map.PopulationOf(*Map.Find(CityId));
	const float MaxDefence = Map.EffectiveMaxDefence(*Map.Find(CityId));

	const bool bFell = Map.Damage(CityId, 1.0e7f, 0.0f);
	FCataclysmDayReport Report;
	Run->NoteTheBailouts(Report);
	const FCataclysmCity& Saved = *Map.Find(CityId);
	const bool bMarked = Saved.bBailedOut;
	const float Defence = Map.DefenceOf(Saved);
	const float PeopleAfter = Map.PopulationOf(Saved);
	const bool bFellAgain = Map.Damage(CityId, 1.0e7f, 0.0f);
	AddInfo(FString::Printf(TEXT("BAILOUT SAVED: fell %d, marked %d, defence %.2f of %.2f, people %.2f of %.2f, report %d; "
								 "the second hit fell %d; asked %d"),
							bFell ? 1 : 0, bMarked ? 1 : 0, Defence, MaxDefence, PeopleAfter, People,
							Report.BailedOut.Num(), bFellAgain ? 1 : 0, Asked));

	TestFalse(TEXT("the city does not fall"), bFell);
	TestTrue(TEXT("and is marked saved"), bMarked);
	TestEqual(TEXT("at half its maximum defence"), Defence, 0.5f * MaxDefence, 0.01f);
	TestEqual(TEXT("with its people as they were"), PeopleAfter, People, 0.01f);
	TestTrue(TEXT("the day's report lists it"), Report.BailedOut.Num() == 1 && Report.BailedOut[0] == CityId);
	TestTrue(TEXT("the fee was asked for once, for an Outpost"), Asked == 1 && AskedFor == ECataclysmCityTier::Outpost);
	TestTrue(TEXT("a second fatal hit fells it"), bFellAgain && Map.Find(CityId)->bFallen);
	TestEqual(TEXT("without asking again"), Asked, 1);
	return true;
}

/**
 * What the bailout never saves: the Pillar, whose loss ends the run, falls without the fee being asked for; a tenth Void
 * stack fells a city and the fee is not asked for (V6); a Sanctuary whose Last Stand is armed holds at the Last Stand's
 * 1 and the fee is not asked for (V7); an Outpost whose fee is not paid falls unmarked (V9). CONTROL: without the node an
 * Outpost falls and nothing is asked for. Prints "BAILOUT REFUSED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeBailoutRefusedTest,
	"Cataclysm.EmpireTreeBailout.ThePillarTheVoidALastStandAndAnUnpaidFeeAreNotSaved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeBailoutRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	FCataclysmEmpireTreeBonuses Bailout;
	Bailout.CityBailoutPoints = 1;
	Bailout.SanctuarySurvivesAt = 1.0f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Bailout);
	UCataclysmEmpireRun* Bare = QuietRun(2741);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const TArray<FCataclysmCity*> Sanctuaries = AllOf(*Run, ECataclysmCityTier::Sanctuary);
	const TArray<FCataclysmCity*> BareOutposts = AllOf(*Bare, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: two Outposts, a Sanctuary and a Pillar, and an Outpost in the bare run"),
				  Outposts.Num() >= 2 && Sanctuaries.Num() >= 1 && Run->Map->PillarId != INDEX_NONE
					  && BareOutposts.Num() >= 1))
	{
		return false;
	}
	const int32 Voided = Outposts[0]->CityId;
	const int32 Unpaid = Outposts[1]->CityId;
	const int32 Sanctuary = Sanctuaries[0]->CityId;
	const int32 Pillar = Run->Map->PillarId;
	int32 Asked = 0;
	bool bPay = true;
	Run->PayTheBailout = [&Asked, &bPay](UCataclysmEmpireRun&, int32, ECataclysmCityTier)
	{
		++Asked;
		return bPay;
	};
	int32 BareAsked = 0;
	Bare->PayTheBailout = [&BareAsked](UCataclysmEmpireRun&, int32, ECataclysmCityTier)
	{
		++BareAsked;
		return true;
	};
	UCataclysmEmpireMap& Map = *Run->Map;

	const bool bPillarFell = Map.Damage(Pillar, 1.0e7f, 0.0f);
	const int32 AskedForThePillar = Asked;
	const bool bVoidFell = Map.AddVoidStacks(Voided, UCataclysmEmpireMap::VoidStacksToErase);
	const int32 AskedForTheVoid = Asked - AskedForThePillar;
	Run->ArmTheLastStandFor(Sanctuary);
	const bool bSanctuaryFell = Map.Damage(Sanctuary, 1.0e7f, 0.0f);
	Run->NoteTheLastStand();
	const float SanctuaryDefence = Map.DefenceOf(*Map.Find(Sanctuary));
	const int32 AskedForTheSanctuary = Asked - AskedForThePillar - AskedForTheVoid;
	bPay = false;
	const bool bUnpaidFell = Map.Damage(Unpaid, 1.0e7f, 0.0f);
	const int32 AskedForTheUnpaid = Asked - AskedForThePillar - AskedForTheVoid - AskedForTheSanctuary;
	const bool bBareFell = Bare->Map->Damage(BareOutposts[0]->CityId, 1.0e7f, 0.0f);
	AddInfo(FString::Printf(TEXT("BAILOUT REFUSED: the Pillar fell %d, asked %d; the Void fell %d, asked %d; the "
								 "Sanctuary fell %d at %.2f, asked %d; unpaid fell %d, marked %d, asked %d; bare fell %d, "
								 "asked %d"),
							bPillarFell ? 1 : 0, AskedForThePillar, bVoidFell ? 1 : 0, AskedForTheVoid,
							bSanctuaryFell ? 1 : 0, SanctuaryDefence, AskedForTheSanctuary, bUnpaidFell ? 1 : 0,
							Map.Find(Unpaid)->bBailedOut ? 1 : 0, AskedForTheUnpaid, bBareFell ? 1 : 0, BareAsked));

	TestTrue(TEXT("the Pillar falls"), bPillarFell);
	TestEqual(TEXT("without the fee being asked for"), AskedForThePillar, 0);
	TestTrue(TEXT("a tenth Void stack fells a city"), bVoidFell);
	TestEqual(TEXT("and is not health hitting 0: no fee asked for"), AskedForTheVoid, 0);
	TestFalse(TEXT("an armed Last Stand holds the Sanctuary"), bSanctuaryFell);
	TestEqual(TEXT("at its 1"), SanctuaryDefence, 1.0f, 0.001f);
	TestEqual(TEXT("before the bailout is asked"), AskedForTheSanctuary, 0);
	TestTrue(TEXT("an unpaid fee lets the city fall"), bUnpaidFell);
	TestFalse(TEXT("unmarked, so its once is not spent"), Map.Find(Unpaid)->bBailedOut);
	TestEqual(TEXT("after the fee was asked for once"), AskedForTheUnpaid, 1);
	TestTrue(TEXT("CONTROL: without the node an Outpost falls"), bBareFell);
	TestEqual(TEXT("and nothing is asked for"), BareAsked, 0);
	return true;
}

/**
 * The fee (the ruling of 2026-11-21): 50,000 tier 1 gold for an Outpost, 100,000 for a Bulwark, 150,000 for a
 * Sanctuary, and none for the Pillar, which cannot be saved this way. THE PAYMENT THROUGH THE GAME INSTANCE IS NOT
 * REACHED HERE: `UCataclysmGameInstance::WireTheRunToTheBalance`'s callback reads the balance with the game instance as
 * its world context, and a game instance a test makes has no world context, so it would find no balance; the game's
 * own instance has one. Prints "BAILOUT FEE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeBailoutFeeTest,
	"Cataclysm.EmpireTreeBailout.TheFeeIsFiftyAHundredOrAHundredAndFiftyThousandAndNoneForThePillar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeBailoutFeeTest::RunTest(const FString&)
{
	const int64 Outpost = UCataclysmEmpireTree::BailoutFeeAtTierOneFor(ECataclysmCityTier::Outpost);
	const int64 Bulwark = UCataclysmEmpireTree::BailoutFeeAtTierOneFor(ECataclysmCityTier::Bulwark);
	const int64 Sanctuary = UCataclysmEmpireTree::BailoutFeeAtTierOneFor(ECataclysmCityTier::Sanctuary);
	const int64 Pillar = UCataclysmEmpireTree::BailoutFeeAtTierOneFor(ECataclysmCityTier::Pillar);
	AddInfo(FString::Printf(TEXT("BAILOUT FEE: %lld, %lld, %lld, %lld"), Outpost, Bulwark, Sanctuary, Pillar));

	TestEqual(TEXT("an Outpost's fee: 50,000"), Outpost, static_cast<int64>(50000));
	TestEqual(TEXT("a Bulwark's: 100,000"), Bulwark, static_cast<int64>(100000));
	TestEqual(TEXT("a Sanctuary's: 150,000"), Sanctuary, static_cast<int64>(150000));
	TestEqual(TEXT("the Pillar's: none"), Pillar, static_cast<int64>(0));
	return true;
}

// ---------------------------------------------------------------------------
// Part c: Bribery
// ---------------------------------------------------------------------------

/**
 * The run's half of Bribery (V12, V13): it refuses, saying why, no such dungeon, the Cataclysm's own dungeon, a Quest
 * dungeon (its timer does not resolve) and a timer already at its start; it bribes an ordinary dungeon whose timer has
 * run 20 of its 30 days, back to 30, and spends the surge's one bribe, so a second is refused; the next surge gives it
 * back. Prints "BRIBERY RUN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeBriberyRunTest,
	"Cataclysm.EmpireTreeBribery.TheRunResetsOneTimerASurgeAndSaysWhyItRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeBriberyRunTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const TArray<FCataclysmCity*> Bulwarks = AllOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestTrue(TEXT("set-up: an Outpost and a Bulwark"), Outposts.Num() > 0 && Bulwarks.Num() > 0))
	{
		return false;
	}
	Place(*Run, 9501, ECataclysmDungeonType::Basic, *Outposts[0], 30.0f);
	Place(*Run, 9502, ECataclysmDungeonType::Basic, *Bulwarks[0], 30.0f);
	Place(*Run, 9503, ECataclysmDungeonType::Quest, *Bulwarks[0], 30.0f);
	Place(*Run, 9504, ECataclysmDungeonType::Cataclysm, *Bulwarks[0], 30.0f);
	TimerOf(*Run, 9501)->DaysUntilResolve = 10.0f;

	const FString NoSuch = Run->RefusalForBribing(424242);
	const FString Boss = Run->RefusalForBribing(9504);
	const FString Quest = Run->RefusalForBribing(9503);
	const FString AtStart = Run->RefusalForBribing(9502);
	const FString Allowed = Run->RefusalForBribing(9501);
	const bool bBribed = Run->BribeDungeon(9501);
	const float Reset = TimerOf(*Run, 9501)->DaysUntilResolve;
	const bool bSpent = Run->bBribedThisSurge;
	TimerOf(*Run, 9502)->DaysUntilResolve = 5.0f;
	const FString Second = Run->RefusalForBribing(9502);
	const bool bSecondBribed = Run->BribeDungeon(9502);
	Run->Surges->NextSurgeDay = static_cast<float>(Run->Day() + 1);
	const FCataclysmDayReport Surged = Run->AdvanceDay();
	const bool bSpentAfterTheSurge = Run->bBribedThisSurge;
	AddInfo(FString::Printf(TEXT("BRIBERY RUN: [%s] [%s] [%s] [%s] [%s]; bribed %d to %.2f, spent %d; second [%s] %d; "
								 "surged %d, spent %d"),
							*NoSuch, *Boss, *Quest, *AtStart, *Allowed, bBribed ? 1 : 0, Reset, bSpent ? 1 : 0, *Second,
							bSecondBribed ? 1 : 0, Surged.bSurged ? 1 : 0, bSpentAfterTheSurge ? 1 : 0));

	TestEqual(TEXT("no such dungeon"), NoSuch, FString(TEXT("There is no such dungeon on the map.")));
	TestEqual(TEXT("the Cataclysm's own dungeon"), Boss, FString(TEXT("The Cataclysm's own dungeon cannot be bribed.")));
	TestEqual(TEXT("a Quest dungeon's timer does not resolve"), Quest,
			  FString(TEXT("Only a dungeon whose timer resolves can be bribed.")));
	TestEqual(TEXT("a timer at its start"), AtStart, FString(TEXT("Its timer is already at its start.")));
	TestTrue(TEXT("an ordinary dungeon 20 days down may be bribed"), Allowed.IsEmpty() && bBribed);
	TestEqual(TEXT("its timer is back at its start, 30"), Reset, 30.0f, 0.001f);
	TestTrue(TEXT("and the surge's bribe is spent"), bSpent);
	TestEqual(TEXT("a second this surge is refused"), Second,
			  FString(TEXT("Bribery has been used this surge; it can be used again after the next surge.")));
	TestFalse(TEXT("and not made"), bSecondBribed);
	TestTrue(TEXT("set-up: a surge fired"), Surged.bSurged);
	TestFalse(TEXT("which gives the bribe back"), bSpentAfterTheSurge);
	return true;
}

/**
 * The game's half (V13, V14): without Bribery held, "Bribery is not held in the empire upgrade tree."; held, with 5,000
 * against the fee of 20,000 at tier 1, "Costs 20,000 gold; 15,000 more needed." and nothing taken; with 30,000, the
 * dungeon is bribed, 20,000 is taken and its timer is at its start. Prints "BRIBERY FEE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeBriberyFeeTest,
	"Cataclysm.EmpireTreeBribery.ABribeNeedsTheNodeAndTheWholeFee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeBriberyFeeTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	FNineScene Scene = MakeScene();
	ON_SCOPE_EXIT
	{
		EndScene(Scene);
	};
	UCataclysmAccountSave* Account = Scene.IsUsable() ? Scene.Account() : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	// AN ORDINARY DUNGEON PUT ON AN OUTPOST BY HAND, as the run's half does: `Begin` leaves the board empty until the
	// first day is spent, and this scene spends none.
	const TArray<FCataclysmCity*> Outposts = AllOf(*Scene.Run, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: an Outpost"), Outposts.Num() > 0))
	{
		return false;
	}
	const int32 DungeonId = 9601;
	Place(*Scene.Run, DungeonId, ECataclysmDungeonType::Basic, *Outposts[0], 30.0f);
	FCataclysmDungeonTimer* Timer = TimerOf(*Scene.Run, DungeonId);
	if (!TestNotNull(TEXT("set-up: its timer"), Timer))
	{
		return false;
	}
	Timer->DaysUntilResolve = Timer->ResolveDays - 3.0f;

	Account->Gold = 5000;
	const FString NotHeld = FCataclysmDungeonBribe::RefusalFor(Scene.World, Scene.Run, DungeonId);
	Account->EmpireTree.Add(Bribery, 1);
	const FString Short = FCataclysmDungeonBribe::Bribe(Scene.World, Scene.Run, DungeonId);
	const int64 ShortHeld = Account->Gold;
	Account->Gold = 30000;
	const FString Paid = FCataclysmDungeonBribe::Bribe(Scene.World, Scene.Run, DungeonId);
	const int64 Held = Account->Gold;
	const float Days = TimerOf(*Scene.Run, DungeonId)->DaysUntilResolve;
	const float Start = TimerOf(*Scene.Run, DungeonId)->ResolveDays;
	AddInfo(FString::Printf(TEXT("BRIBERY FEE: fee %lld; [%s]; [%s] %lld held; [%s] %lld held, timer %.2f of %.2f"),
							FCataclysmDungeonBribe::FeeIn(Scene.Run), *NotHeld, *Short, ShortHeld, *Paid, Held, Days,
							Start));

	TestEqual(TEXT("the fee at tier 1: 20,000"), FCataclysmDungeonBribe::FeeIn(Scene.Run), static_cast<int64>(20000));
	TestEqual(TEXT("without the node"), NotHeld, FString(TEXT("Bribery is not held in the empire upgrade tree.")));
	TestEqual(TEXT("held, 5,000 is short"), Short, FString(TEXT("Costs 20,000 gold; 15,000 more needed.")));
	TestEqual(TEXT("and nothing is taken"), ShortHeld, static_cast<int64>(5000));
	TestTrue(TEXT("30,000 bribes it"), Paid.IsEmpty());
	TestEqual(TEXT("taking 20,000"), Held, static_cast<int64>(10000));
	TestEqual(TEXT("its timer at its start"), Days, Start, 0.001f);
	TestTrue(TEXT("and the surge's bribe spent"), Scene.Run->bBribedThisSurge);
	return true;
}

// ---------------------------------------------------------------------------
// Part c: Scout Parties
// ---------------------------------------------------------------------------

/**
 * With a chance of 1, a clear of an ordinary dungeon on an Outpost discovers a bonus dungeon on that Outpost (V15, V18):
 * marked a bonus, Basic, no Siege, with a timer, and it is what the clear says it added; the run's own three streams
 * are where they were (V15). A clear of the bonus dungeon discovers none. CONTROL: a chance of 0 discovers none, and the
 * streams are where they were too. Prints "SCOUTS FOUND".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScoutsFoundTest,
	"Cataclysm.EmpireTreeScouts.AClearMayDiscoverABonusDungeonOnTheSameCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScoutsFoundTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	FCataclysmEmpireTreeBonuses Scouts;
	Scouts.BonusDungeonChance = 1.0f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Scouts);
	UCataclysmEmpireRun* Control = QuietRun(2741);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const TArray<FCataclysmCity*> ControlOutposts = AllOf(*Control, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: an Outpost in each run"), Outposts.Num() > 0 && ControlOutposts.Num() > 0))
	{
		return false;
	}
	const int32 CityId = Outposts[0]->CityId;
	Place(*Run, 9601, ECataclysmDungeonType::Basic, *Outposts[0], 30.0f);
	Place(*Control, 9601, ECataclysmDungeonType::Basic, *ControlOutposts[0], 30.0f);
	const int32 StreamBefore = Run->Stream.GetCurrentSeed();
	const int32 ModifiersBefore = Run->ModifierStream.GetCurrentSeed();
	const int32 CataclysmsBefore = Run->CataclysmStream.GetCurrentSeed();

	Run->ClearDungeon(9601);
	const int32 Added = Run->LastBonusDungeonAdded;
	const FCataclysmDungeon* Bonus = Run->FindDungeon(Added);
	const bool bBonus = Bonus != nullptr && Bonus->bBonus;
	const bool bBasic = Bonus != nullptr && Bonus->Type == ECataclysmDungeonType::Basic;
	const bool bNoSiege = Bonus != nullptr && Bonus->SubType != ECataclysmDungeonSubType::Siege;
	const bool bOnTheCity = Bonus != nullptr && Bonus->CityId == CityId;
	const bool bTimed = TimerOf(*Run, Added) != nullptr;
	const bool bStreams = Run->Stream.GetCurrentSeed() == StreamBefore
		&& Run->ModifierStream.GetCurrentSeed() == ModifiersBefore
		&& Run->CataclysmStream.GetCurrentSeed() == CataclysmsBefore;
	Run->ClearDungeon(Added);
	const int32 AfterTheBonus = Run->LastBonusDungeonAdded;

	const int32 ControlStream = Control->Stream.GetCurrentSeed();
	Control->ClearDungeon(9601);
	AddInfo(FString::Printf(TEXT("SCOUTS FOUND: added %d, bonus %d, basic %d, no siege %d, on city %d %d, timed %d, "
								 "streams kept %d; after the bonus %d; control added %d, stream kept %d"),
							Added, bBonus ? 1 : 0, bBasic ? 1 : 0, bNoSiege ? 1 : 0, CityId, bOnTheCity ? 1 : 0,
							bTimed ? 1 : 0, bStreams ? 1 : 0, AfterTheBonus, Control->LastBonusDungeonAdded,
							Control->Stream.GetCurrentSeed() == ControlStream ? 1 : 0));

	TestTrue(TEXT("a bonus dungeon is added"), Added != INDEX_NONE && Bonus != nullptr);
	TestTrue(TEXT("marked a bonus"), bBonus);
	TestTrue(TEXT("Basic (V18)"), bBasic);
	TestTrue(TEXT("with no Siege"), bNoSiege);
	TestTrue(TEXT("on the same city"), bOnTheCity);
	TestTrue(TEXT("with a timer"), bTimed);
	TestTrue(TEXT("the run's own streams are where they were (V15)"), bStreams);
	TestEqual(TEXT("a bonus dungeon's clear discovers none"), AfterTheBonus, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("CONTROL: a chance of 0 discovers none"), Control->LastBonusDungeonAdded,
			  static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("CONTROL: and its stream is where it was"), Control->Stream.GetCurrentSeed(), ControlStream);
	return true;
}

/**
 * A bonus dungeon's clear counts for what pays the player and not for what moves the empire's pressure (V16, V17, V19):
 * the run's clears, the clears since the last surge and since the last death, and the bonus clears each rise by 1;
 * the ordinary clears and the Cataclysm boss's floors do not move; and the kinds defeated do not change -- 0 to 0 in a
 * run with no ordinary clear, and 1 to 1 in one with an ordinary clear first. Prints "SCOUTS COUNTED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScoutsCountedTest,
	"Cataclysm.EmpireTreeScouts.ABonusClearPaysButMovesNoPressureAndAddsNoKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScoutsCountedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	UCataclysmEmpireRun* After = QuietRun(2741);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const TArray<FCataclysmCity*> AfterOutposts = AllOf(*After, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: an Outpost in each run"), Outposts.Num() > 0 && AfterOutposts.Num() > 0))
	{
		return false;
	}
	const FCataclysmDungeon Asking = DungeonOn(Outposts[0]->CityId);
	const FCataclysmDungeon AfterAsking = DungeonOn(AfterOutposts[0]->CityId);

	// NO ORDINARY CLEAR BEFORE IT.
	Place(*Run, 9701, ECataclysmDungeonType::Basic, *Outposts[0], 30.0f, /*bBonus*/ true);
	const int32 KindsBefore = UCataclysmEmpireTree::DungeonStateFor(*Run, Asking, {}, 1).KindsDefeated;
	const int32 Cleared = Run->DungeonsCleared;
	const int32 SinceSurge = Run->ClearsSinceLastSurge;
	const int32 SinceDeath = Run->ClearsSinceLastDeath;
	const int32 Ordinary = Run->BasicDungeonsCleared;
	const int32 Floors = Run->CataclysmDungeonFloorsNow();
	Run->ClearDungeon(9701);
	const int32 KindsAfter = UCataclysmEmpireTree::DungeonStateFor(*Run, Asking, {}, 1).KindsDefeated;

	// AN ORDINARY CLEAR FIRST.
	Place(*After, 9702, ECataclysmDungeonType::Basic, *AfterOutposts[0], 30.0f);
	After->ClearDungeon(9702);
	const int32 KindsWithOne = UCataclysmEmpireTree::DungeonStateFor(*After, AfterAsking, {}, 1).KindsDefeated;
	Place(*After, 9703, ECataclysmDungeonType::Basic, *AfterOutposts[0], 30.0f, /*bBonus*/ true);
	After->ClearDungeon(9703);
	const int32 KindsWithOneAfter = UCataclysmEmpireTree::DungeonStateFor(*After, AfterAsking, {}, 1).KindsDefeated;
	AddInfo(FString::Printf(TEXT("SCOUTS COUNTED: cleared %d to %d, since the surge %d to %d, since a death %d to %d, "
								 "bonus %d, ordinary %d to %d, boss floors %d to %d; kinds %d to %d, and with an ordinary "
								 "clear %d to %d"),
							Cleared, Run->DungeonsCleared, SinceSurge, Run->ClearsSinceLastSurge, SinceDeath,
							Run->ClearsSinceLastDeath, Run->BonusDungeonsCleared, Ordinary, Run->BasicDungeonsCleared,
							Floors, Run->CataclysmDungeonFloorsNow(), KindsBefore, KindsAfter, KindsWithOne,
							KindsWithOneAfter));

	TestEqual(TEXT("the run's clears rise by 1"), Run->DungeonsCleared, Cleared + 1);
	TestEqual(TEXT("so do the clears since the last surge, which Global Vigilance reads"), Run->ClearsSinceLastSurge,
			  SinceSurge + 1);
	TestEqual(TEXT("and since the last death, which Relentless Pursuit reads"), Run->ClearsSinceLastDeath,
			  SinceDeath + 1);
	TestEqual(TEXT("and the bonus clears"), Run->BonusDungeonsCleared, 1);
	TestEqual(TEXT("the ordinary clears do not move"), Run->BasicDungeonsCleared, Ordinary);
	TestEqual(TEXT("nor the Cataclysm boss's floors"), Run->CataclysmDungeonFloorsNow(), Floors);
	TestEqual(TEXT("no ordinary clear before: the kinds stay as they were"), KindsAfter, KindsBefore);
	TestEqual(TEXT("set-up: an ordinary clear is one kind"), KindsWithOne, 1);
	TestEqual(TEXT("an ordinary clear before: the kinds stay at 1"), KindsWithOneAfter, KindsWithOne);
	return true;
}

/**
 * A bonus dungeon leaves with no consequence (V16): it is not counted among the dungeons that threaten, and its city
 * is scheduled to lose nothing by it; when it resolves -- the path a timer running out and a death in it share -- it
 * leaves the map, its city loses no defence and no people, and no detonation is counted. CONTROL: an ordinary dungeon
 * beside it threatens, is scheduled to take something, and resolving it costs its city and counts a detonation. Prints
 * "SCOUTS LEFT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScoutsLeftTest,
	"Cataclysm.EmpireTreeScouts.ABonusDungeonLeavesWithNoConsequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScoutsLeftTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const TArray<FCataclysmCity*> Bulwarks = AllOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestTrue(TEXT("set-up: an Outpost and a Bulwark"), Outposts.Num() > 0 && Bulwarks.Num() > 0))
	{
		return false;
	}
	const int32 BonusCity = Outposts[0]->CityId;
	const int32 OrdinaryCity = Bulwarks[0]->CityId;
	Place(*Run, 9801, ECataclysmDungeonType::Basic, *Outposts[0], 30.0f, /*bBonus*/ true);
	Place(*Run, 9802, ECataclysmDungeonType::Basic, *Bulwarks[0], 30.0f);
	UCataclysmEmpireMap& Map = *Run->Map;

	const int32 Threatening = Run->ThreateningDungeonCount();
	const float BonusLoss = Run->ScheduledDefenceLossOn(BonusCity, 100);
	const float OrdinaryLoss = Run->ScheduledDefenceLossOn(OrdinaryCity, 100);
	const float DefenceBefore = Map.DefenceOf(*Map.Find(BonusCity));
	const float PeopleBefore = Map.PopulationOf(*Map.Find(BonusCity));
	const int32 Detonated = Run->DungeonsDetonated;
	Run->ResolveDungeonOnDeath(9801);
	const bool bGone = Run->FindDungeon(9801) == nullptr;
	const float DefenceAfter = Map.DefenceOf(*Map.Find(BonusCity));
	const float PeopleAfter = Map.PopulationOf(*Map.Find(BonusCity));
	const int32 DetonatedAfter = Run->DungeonsDetonated;

	const float OrdinaryBefore = Map.DefenceOf(*Map.Find(OrdinaryCity));
	Run->ResolveDungeonOnDeath(9802);
	const float OrdinaryAfter = Map.DefenceOf(*Map.Find(OrdinaryCity));
	AddInfo(FString::Printf(TEXT("SCOUTS LEFT: threatening %d of 2; scheduled %.2f and %.2f; gone %d, defence %.2f to "
								 "%.2f, people %.2f to %.2f, detonated %d to %d; the ordinary one %.2f to %.2f, detonated "
								 "%d"),
							Threatening, BonusLoss, OrdinaryLoss, bGone ? 1 : 0, DefenceBefore, DefenceAfter,
							PeopleBefore, PeopleAfter, Detonated, DetonatedAfter, OrdinaryBefore, OrdinaryAfter,
							Run->DungeonsDetonated));

	TestEqual(TEXT("one of the two threatens"), Threatening, 1);
	TestEqual(TEXT("its city is scheduled to lose nothing by it"), BonusLoss, 0.0f, 0.001f);
	TestTrue(TEXT("CONTROL: the ordinary one's city is"), OrdinaryLoss > 0.0f);
	TestTrue(TEXT("resolved, it leaves the map"), bGone);
	TestEqual(TEXT("its city's defence is as it was"), DefenceAfter, DefenceBefore, 0.001f);
	TestEqual(TEXT("and its people"), PeopleAfter, PeopleBefore, 0.001f);
	TestEqual(TEXT("and no detonation is counted"), DetonatedAfter, Detonated);
	TestTrue(TEXT("CONTROL: the ordinary one costs its city"), OrdinaryAfter < OrdinaryBefore);
	TestEqual(TEXT("CONTROL: and counts a detonation"), Run->DungeonsDetonated, DetonatedAfter + 1);
	return true;
}

// ---------------------------------------------------------------------------
// The three records' steps
// ---------------------------------------------------------------------------

/**
 * The files from before slice 9 migrate to nothing spent, saved or bribed, and the committed files read the figures
 * they state. `Account_v6.json` migrates with 0 spent, keeping its 1234567 gold and its 2500 owed; `Character_v13.json`
 * with 0 spent, keeping its 1500 owed; `Run_v14.json` with no city bailed out, no dungeon a bonus one, no bonus clear
 * and no bribe, keeping its 3 clears since the last surge. `Account_v7.json` reads 75000.5 spent, `Character_v14.json`
 * 50000.25 (unrounded doubles, V20), and `Run_v15.json` its first city bailed out and its second
 * not, 2 bonus clears and the bribe spent, each read
 * rather than migrated. Prints "NINE FILES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeNineSavesFilesTest,
	"Cataclysm.EmpireTreeNineSaves.ThePreviousFilesMigrateAndTheCommittedOnesReadTheirFigures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeNineSavesFilesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeNineTest;

	ECataclysmSaveLoadResult OldAccountResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult OldCharacterResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult OldRunResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult AccountResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult CharacterResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult RunResult = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmAccountSave* OldAccount =
		ReadFixture<UCataclysmAccountSave>(*this, TEXT("Account_v6.json"), OldAccountResult);
	const UCataclysmCharacterSave* OldCharacter =
		ReadFixture<UCataclysmCharacterSave>(*this, TEXT("Character_v13.json"), OldCharacterResult);
	const UCataclysmRunSave* OldRun = ReadFixture<UCataclysmRunSave>(*this, TEXT("Run_v14.json"), OldRunResult);
	const UCataclysmAccountSave* Account =
		ReadFixture<UCataclysmAccountSave>(*this, TEXT("Account_v7.json"), AccountResult);
	const UCataclysmCharacterSave* Character =
		ReadFixture<UCataclysmCharacterSave>(*this, TEXT("Character_v14.json"), CharacterResult);
	const UCataclysmRunSave* Run = ReadFixture<UCataclysmRunSave>(*this, TEXT("Run_v15.json"), RunResult);
	if (!OldAccount || !OldCharacter || !OldRun || !Account || !Character || !Run)
	{
		return false;
	}
	bool bNoneBailedOut = OldRun->Cities.Num() > 0;
	for (const FCataclysmCity& City : OldRun->Cities)
	{
		bNoneBailedOut &= !City.bBailedOut;
	}
	bool bNoBonus = OldRun->Dungeons.Num() > 0;
	for (const FCataclysmDungeon& Dungeon : OldRun->Dungeons)
	{
		bNoBonus &= !Dungeon.bBonus;
	}
	AddInfo(FString::Printf(TEXT("NINE FILES: account v6 %s, %.2f spent, %lld gold, %lld owed; character v13 %s, %.2f "
								 "spent, %lld owed; run v14 %s, bailed out none %d, bonus none %d, %d bonus clears, bribed "
								 "%d, %d since the surge; account v7 %s %.2f; character v14 %s %.2f; run v15 %s, %d cities "
								 "first %d second %d, %d, %d"),
							FCataclysmSaveStorage::Describe(OldAccountResult), OldAccount->GoldSpentOnCityUpgrades,
							OldAccount->Gold, OldAccount->GoldOwed, FCataclysmSaveStorage::Describe(OldCharacterResult),
							OldCharacter->GoldSpentOnCityUpgrades, OldCharacter->GoldOwed,
							FCataclysmSaveStorage::Describe(OldRunResult), bNoneBailedOut ? 1 : 0, bNoBonus ? 1 : 0,
							OldRun->BonusDungeonsCleared, OldRun->bBribedThisSurge ? 1 : 0, OldRun->ClearsSinceLastSurge,
							FCataclysmSaveStorage::Describe(AccountResult), Account->GoldSpentOnCityUpgrades,
							FCataclysmSaveStorage::Describe(CharacterResult), Character->GoldSpentOnCityUpgrades,
							FCataclysmSaveStorage::Describe(RunResult), Run->Cities.Num(),
							Run->Cities.Num() > 0 && Run->Cities[0].bBailedOut ? 1 : 0,
							Run->Cities.Num() > 1 && Run->Cities[1].bBailedOut ? 1 : 0, Run->BonusDungeonsCleared,
							Run->bBribedThisSurge ? 1 : 0));

	const int32 Migrated = static_cast<int32>(ECataclysmSaveLoadResult::Migrated);
	const int32 Loaded = static_cast<int32>(ECataclysmSaveLoadResult::Loaded);
	TestEqual(TEXT("Account_v6.json is migrated"), static_cast<int32>(OldAccountResult), Migrated);
	TestEqual(TEXT("to nothing spent"), OldAccount->GoldSpentOnCityUpgrades, 0.0);
	TestTrue(TEXT("keeping its gold and its debt"), OldAccount->Gold == 1234567 && OldAccount->GoldOwed == 2500);
	TestEqual(TEXT("Character_v13.json is migrated"), static_cast<int32>(OldCharacterResult), Migrated);
	TestEqual(TEXT("to nothing spent"), OldCharacter->GoldSpentOnCityUpgrades, 0.0);
	TestEqual(TEXT("keeping its debt"), OldCharacter->GoldOwed, static_cast<int64>(1500));
	TestEqual(TEXT("Run_v14.json is migrated"), static_cast<int32>(OldRunResult), Migrated);
	TestTrue(TEXT("to no city bailed out"), bNoneBailedOut);
	TestTrue(TEXT("no dungeon a bonus one"), bNoBonus);
	TestEqual(TEXT("no bonus clear"), OldRun->BonusDungeonsCleared, 0);
	TestFalse(TEXT("and no bribe"), OldRun->bBribedThisSurge);
	TestEqual(TEXT("keeping its clears since the last surge"), OldRun->ClearsSinceLastSurge, 3);
	TestEqual(TEXT("Account_v7.json is read"), static_cast<int32>(AccountResult), Loaded);
	TestEqual(TEXT("with 75000.5 spent, the fraction kept"), Account->GoldSpentOnCityUpgrades, 75000.5);
	TestEqual(TEXT("Character_v14.json is read"), static_cast<int32>(CharacterResult), Loaded);
	TestEqual(TEXT("with 50000.25 spent, the fraction kept"), Character->GoldSpentOnCityUpgrades, 50000.25);
	TestEqual(TEXT("Run_v15.json is read"), static_cast<int32>(RunResult), Loaded);
	TestTrue(TEXT("its first city bailed out and its second not"),
			 Run->Cities.Num() >= 2 && Run->Cities[0].bBailedOut && !Run->Cities[1].bBailedOut);
	TestEqual(TEXT("2 bonus clears"), Run->BonusDungeonsCleared, 2);
	TestTrue(TEXT("and the bribe spent"), Run->bBribedThisSurge);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
