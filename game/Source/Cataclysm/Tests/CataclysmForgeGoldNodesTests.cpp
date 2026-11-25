// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE EMPIRE TREE'S FIVE FORGE GOLD NODES. Issue #47, slice F6a, ruled by the coordinating session on 2026-11-22 (Q1 to
 * Q10, Architectural Synergy's 30% cap the owner's) and 2026-11-24 (A: the pricing is a trailing parameter defaulting
 * to no discount; B: an option's gold is discounted at its own material's tier, one Forge Upgrades roll a craft).
 *
 * Workshop Upgrades 1.5% a point (12% at 8), Apprentice Oversight 20% on a tier 1 or 2 material, Architectural Synergy
 * 1% a point for each 10 Treasury points (30% at most), Treasury Synergy 1% for each 50,000 tier 1 gold spent at the
 * forge (30% at most): added, held at 75% (Q1, as P1). Then Forge Upgrades' 1% a point chance of a tenth more (Q9).
 *
 * WRITTEN BEFORE A BUILD. The scene is `CataclysmForgeMilestonesTests.cpp`'s, copied into this file's namespace.
 */
namespace CataclysmForgeGoldNodesTest
{
	/** The five nodes' ids in `docs/Empire_Development_Tree_Final.json`, and their full points. */
	const FName WorkshopUpgrades(TEXT("basic_arts_t1f"));			// 8 points
	const FName ApprenticeOversight(TEXT("keystone_00000051"));	// 1
	const FName ForgeUpgrades(TEXT("basic_arts_t2e"));			// 5
	const FName ArchitecturalSynergy(TEXT("basic_arts_t3g"));		// 10
	const FName TreasurySynergy(TEXT("keystone_0000005j"));		// 1

	/** Treasury branch nodes, for Architectural Synergy's steps: Coinage, Master Trader and War Funding hold 10 each,
	 *  Tax Collection and Merchant Guild 8 each. */
	const FName Coinage(TEXT("basic_0000003q"));
	const FName MasterTrader(TEXT("basic_0000003r"));
	const FName WarFunding(TEXT("basic_0000003s"));
	const FName TaxCollection(TEXT("basic_tres_t1d"));
	const FName MerchantGuild(TEXT("basic_tres_t1e"));

	/** A helm at a gear level carrying 250 residue, with the kill count that picks its draws (the milestones' helm). */
	FCataclysmItem UpgradeHelm(int32 GearLevel, int32 Kills)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = GearLevel;
		Item.Residue = 250.0f;
		Item.Kills = Kills;
		FCataclysmSocket& Socket = Item.Sockets.AddDefaulted_GetRef();
		Socket.Type = ECataclysmSocketType::Defense;
		return Item;
	}

	/** The first kill count from 0 that `Wanted` accepts, or INDEX_NONE within `Most`. */
	int32 KillsWhere(TFunctionRef<bool(int32)> Wanted, int32 Most = 20000)
	{
		for (int32 Kills = 0; Kills < Most; ++Kills)
		{
			if (Wanted(Kills))
			{
				return Kills;
			}
		}
		return INDEX_NONE;
	}

	/** A pricing with every tier at 1 but these, and the crafting material table, so a material's tier is looked up. */
	FCataclysmForgePricing PricingWith(float TierOne, float TierTwo, float TierThree)
	{
		FCataclysmForgePricing Out;
		Out.Factors[1] = TierOne;
		Out.Factors[2] = TierTwo;
		Out.Factors[3] = TierThree;
		Out.MaterialTable = UCataclysmDropRoll::LoadCraftingMaterialTable();
		return Out;
	}

	struct FGoldNodesScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && Character && State && Character->GetInventory() && Character->GetEquipment()
				&& Run && Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }

		/** The solo character's own empire tree, holding these nodes at these points and nothing else. */
		void Hold(TArray<TPair<FName, int32>> Nodes) const
		{
			FCataclysmPassiveAllocation& Tree = State->EditOwnEmpireTree();
			Tree = FCataclysmPassiveAllocation();
			for (const TPair<FName, int32>& Node : Nodes)
			{
				Tree.Add(Node.Key, Node.Value);
			}
		}
	};

	/** A world with this project's game instance, a possessed solo player with a million gold of its own, and a run. */
	FGoldNodesScene MakeScene()
	{
		FGoldNodesScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
			FCataclysmCharacterPartition Partition;
			Partition.Lethality = ECataclysmLethality::Standard;
			Partition.Population = ECataclysmPopulation::Offline;
			// SOLO SELF-FOUND: its empire tree, its gold and its forge count are its own, on its player state.
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Treasurer"), Partition);
			Out.State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 4706, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}

	/** One upgrade of a fresh helm at +2 with `Kills`: the result, the gold paid and the forge count after. */
	struct FPaid
	{
		ECataclysmForgeResult Result = ECataclysmForgeResult::Refused;
		int64 Gold = -1;
		double CountAfter = -1.0;
	};

	FPaid UpgradeOnce(const FGoldNodesScene& Scene, int32 Kills, bool bWithStones = true)
	{
		FPaid Out;
		const int32 Cell = Scene.Bag()->AddItem(UpgradeHelm(2, Kills));
		if (bWithStones)
		{
			Scene.Bag()->AddMaterial(UCataclysmForge::StoneFor(3), 2);
		}
		const int64 GoldBefore = Scene.State->GetOwnGold();
		FCataclysmForgeUpgrade Done;
		FString Message;
		Out.Result = UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
		Out.Gold = GoldBefore - Scene.State->GetOwnGold();
		Out.CountAfter = Scene.State->GetOwnGoldSpentAtTheForge();
		if (Scene.Bag()->ItemAt(Cell))
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return Out;
	}

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

#define CATACLYSM_GOLD_NODES_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// G1. The tree gives the five nodes' figures; Architectural Synergy counts whole Treasury steps and stops at 30%.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Read through the generated tables: Workshop Upgrades at 8 points gives 12%, Apprentice Oversight 20% on its own figure,
 * Forge Upgrades at 5 a 5% chance, Treasury Synergy 1% a step with its 30% cap. Architectural Synergy at its 10 points
 * gives 10% with 10 Treasury points, 30% with 30, and still 30% with 46 (4 steps, held at its cap: the owner's 30%); at 1
 * point with 30 Treasury points it gives 3% (reading A, the owner's of 2026-11-22). CONTROL: no node held gives nought
 * everywhere. Prints "GOLD NODES".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesTreeTest,
	"Cataclysm.ForgeGoldNodes.TheFiveNodesGiveTheirFiguresAndArchitecturalSynergyStopsAtThirtyPercent")
{
	using namespace CataclysmForgeGoldNodesTest;
	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	auto With = [&](TArray<TPair<FName, int32>> Held)
	{
		FCataclysmPassiveAllocation Tree;
		for (const TPair<FName, int32>& Node : Held)
		{
			Tree.Add(Node.Key, Node.Value);
		}
		return UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tree);
	};
	const FCataclysmEmpireTreePlayerBonuses None = With({});
	const FCataclysmEmpireTreePlayerBonuses All = With({{WorkshopUpgrades, 8}, {ApprenticeOversight, 1},
		{ForgeUpgrades, 5}, {TreasurySynergy, 1}, {ArchitecturalSynergy, 10}, {Coinage, 10}, {MasterTrader, 10},
		{WarFunding, 10}});
	const FCataclysmEmpireTreePlayerBonuses OneStep = With({{ArchitecturalSynergy, 10}, {Coinage, 10}});
	const FCataclysmEmpireTreePlayerBonuses FourSteps = With({{ArchitecturalSynergy, 10}, {Coinage, 10},
		{MasterTrader, 10}, {WarFunding, 10}, {TaxCollection, 8}, {MerchantGuild, 8}});
	const FCataclysmEmpireTreePlayerBonuses OnePoint = With({{ArchitecturalSynergy, 1}, {Coinage, 10},
		{MasterTrader, 10}, {WarFunding, 10}});
	AddInfo(FString::Printf(TEXT("GOLD NODES: all held: reduced %.3f, low tier %.3f, a spending step %.3f capped %.2f, "
								 "tenth %.3f; Architectural Synergy at 10 points: 1 step %.3f, 4 steps %.3f; at 1 point "
								 "and 3 steps %.3f; none held %.3f %.3f %.3f %.3f"),
							All.CraftGoldReduced, All.LowTierCraftGoldReduced, All.CraftGoldReducedPerSpendingStep,
							All.CraftGoldSpendingCap, All.CraftGoldTenthOffChance, OneStep.CraftGoldReduced,
							FourSteps.CraftGoldReduced, OnePoint.CraftGoldReduced, None.CraftGoldReduced,
							None.LowTierCraftGoldReduced, None.CraftGoldReducedPerSpendingStep,
							None.CraftGoldTenthOffChance));

	TestEqual(TEXT("**Workshop Upgrades' 12% and Architectural Synergy's 30% at 3 Treasury steps**"),
			  All.CraftGoldReduced, 0.42f, 0.0001f);
	TestEqual(TEXT("**Apprentice Oversight's 20%**"), All.LowTierCraftGoldReduced, 0.20f, 0.0001f);
	TestEqual(TEXT("**Treasury Synergy's 1% a step**"), All.CraftGoldReducedPerSpendingStep, 0.01f, 0.0001f);
	TestEqual(TEXT("and its own 30% cap"), All.CraftGoldSpendingCap, 0.30f, 0.0001f);
	TestEqual(TEXT("**Forge Upgrades' 5% at 5 points**"), All.CraftGoldTenthOffChance, 0.05f, 0.0001f);
	TestEqual(TEXT("Architectural Synergy with one Treasury step: 10%"), OneStep.CraftGoldReduced, 0.10f, 0.0001f);
	TestEqual(TEXT("**with four steps, held at 30%**"), FourSteps.CraftGoldReduced, 0.30f, 0.0001f);
	TestEqual(TEXT("at 1 point and 3 steps, 3% (reading A)"), OnePoint.CraftGoldReduced, 0.03f, 0.0001f);
	TestEqual(TEXT("CONTROL: none held, no reduction"), None.CraftGoldReduced, 0.0f);
	TestEqual(TEXT("CONTROL: none held, no low tier reduction"), None.LowTierCraftGoldReduced, 0.0f);
	TestEqual(TEXT("CONTROL: none held, no spending step"), None.CraftGoldReducedPerSpendingStep, 0.0f);
	TestEqual(TEXT("CONTROL: none held, no tenth"), None.CraftGoldTenthOffChance, 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// G2. The reductions add and are held at 75%, Apprentice Oversight only on tiers 1 and 2; Forge Upgrades' tenth after.
// --------------------------------------------------------------------------------------------------------------------
/**
 * `CraftGoldFactorFor` on bonuses set by hand. Workshop Upgrades' 12% alone gives 0.88 on any tier. Apprentice
 * Oversight's 20% gives 0.80 on tiers 1 and 2 and nothing on tier 3 or on a craft with no material (tier 0). Treasury
 * Synergy with 1,500,000 tier 1 gold spent gives 30%. All four at full on tier 2 are 12 + 20 + 30 + 30 = 92%, held at
 * 75%: 0.25; with Forge Upgrades' tenth, 0.225. The tenth alone gives 0.90. CONTROL: no node, 1. Prints "GOLD FACTOR".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesFactorTest,
	"Cataclysm.ForgeGoldNodes.TheReductionsAddAndAreHeldAtThreeQuartersThenForgeUpgradesTakesATenth")
{
	FCataclysmEmpireTreePlayerBonuses None;
	FCataclysmEmpireTreePlayerBonuses Workshop;
	Workshop.CraftGoldReduced = 0.12f;
	FCataclysmEmpireTreePlayerBonuses Oversight;
	Oversight.LowTierCraftGoldReduced = 0.20f;
	FCataclysmEmpireTreePlayerBonuses Full;
	Full.CraftGoldReduced = 0.12f + 0.30f;
	Full.LowTierCraftGoldReduced = 0.20f;
	Full.CraftGoldReducedPerSpendingStep = 0.01f;
	Full.CraftGoldSpendingCap = 0.30f;
	const double Spent = 1500000.0;

	const float NoNode = UCataclysmEmpireTree::CraftGoldFactorFor(None, 0.0, 2, false);
	const float WorkshopTierFour = UCataclysmEmpireTree::CraftGoldFactorFor(Workshop, 0.0, 4, false);
	const float OversightOne = UCataclysmEmpireTree::CraftGoldFactorFor(Oversight, 0.0, 1, false);
	const float OversightTwo = UCataclysmEmpireTree::CraftGoldFactorFor(Oversight, 0.0, 2, false);
	const float OversightThree = UCataclysmEmpireTree::CraftGoldFactorFor(Oversight, 0.0, 3, false);
	const float OversightNone = UCataclysmEmpireTree::CraftGoldFactorFor(Oversight, 0.0, 0, false);
	const float FullTierTwo = UCataclysmEmpireTree::CraftGoldFactorFor(Full, Spent, 2, false);
	const float FullTenth = UCataclysmEmpireTree::CraftGoldFactorFor(Full, Spent, 2, true);
	const float FullTierThree = UCataclysmEmpireTree::CraftGoldFactorFor(Full, Spent, 3, false);
	const float TenthAlone = UCataclysmEmpireTree::CraftGoldFactorFor(None, 0.0, 2, true);
	AddInfo(FString::Printf(TEXT("GOLD FACTOR: none %.3f; Workshop on tier 4 %.3f; Oversight tiers 1, 2, 3, none %.3f %.3f "
								 "%.3f %.3f; all four tier 2 %.3f, with the tenth %.4f, tier 3 %.3f; tenth alone %.3f"),
							NoNode, WorkshopTierFour, OversightOne, OversightTwo, OversightThree, OversightNone,
							FullTierTwo, FullTenth, FullTierThree, TenthAlone));

	TestEqual(TEXT("CONTROL: no node, the price as it is"), NoNode, 1.0f);
	TestEqual(TEXT("Workshop Upgrades' 12% on any tier"), WorkshopTierFour, 0.88f, 0.0001f);
	TestEqual(TEXT("**Apprentice Oversight on a tier 1 material**"), OversightOne, 0.80f, 0.0001f);
	TestEqual(TEXT("and on tier 2"), OversightTwo, 0.80f, 0.0001f);
	TestEqual(TEXT("**not on tier 3**"), OversightThree, 1.0f);
	TestEqual(TEXT("nor on a craft with no material"), OversightNone, 1.0f);
	TestEqual(TEXT("**all four, 92%, held at 75%**"), FullTierTwo, 0.25f, 0.0001f);
	TestEqual(TEXT("**Forge Upgrades' tenth after the cap**"), FullTenth, 0.225f, 0.0001f);
	TestEqual(TEXT("on tier 3, 72%: under the cap"), FullTierThree, 0.28f, 0.0001f);
	TestEqual(TEXT("the tenth alone"), TenthAlone, 0.90f, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// G3. Treasury Synergy: 1% for each whole 50,000 tier 1 gold spent at the forge, at most 30%.
// --------------------------------------------------------------------------------------------------------------------
/**
 * `CraftGoldFromSpendingFor` with the node held (1% a step, 30% cap): 49,999.99 gives nothing, 50,000 gives 1%,
 * 1,499,999 gives 29%, 1,500,000 gives 30%, 9,000,000 still 30%. CONTROL: without the node, 9,000,000 gives nothing, as
 * the count is kept whether the node is held or not (Q5, as V5). Prints "TREASURY SYNERGY".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesTreasuryTest,
	"Cataclysm.ForgeGoldNodes.TreasurySynergyGivesOnePercentForEachFiftyThousandUpToThirty")
{
	FCataclysmEmpireTreePlayerBonuses Held;
	Held.CraftGoldReducedPerSpendingStep = 0.01f;
	Held.CraftGoldSpendingCap = 0.30f;
	const FCataclysmEmpireTreePlayerBonuses NotHeld;
	const float Under = UCataclysmEmpireTree::CraftGoldFromSpendingFor(Held, 49999.99);
	const float One = UCataclysmEmpireTree::CraftGoldFromSpendingFor(Held, 50000.0);
	const float TwentyNine = UCataclysmEmpireTree::CraftGoldFromSpendingFor(Held, 1499999.0);
	const float Thirty = UCataclysmEmpireTree::CraftGoldFromSpendingFor(Held, 1500000.0);
	const float Beyond = UCataclysmEmpireTree::CraftGoldFromSpendingFor(Held, 9000000.0);
	const float Without = UCataclysmEmpireTree::CraftGoldFromSpendingFor(NotHeld, 9000000.0);
	AddInfo(FString::Printf(TEXT("TREASURY SYNERGY: 49,999.99 %.3f; 50,000 %.3f; 1,499,999 %.3f; 1,500,000 %.3f; "
								 "9,000,000 %.3f; without the node %.3f; a step is %lld"),
							Under, One, TwentyNine, Thirty, Beyond, Without,
							UCataclysmEmpireTree::TreasurySynergyGoldAStep));

	TestEqual(TEXT("set-up: a step is 50,000 tier 1 gold"), UCataclysmEmpireTree::TreasurySynergyGoldAStep,
			  static_cast<int64>(50000));
	TestEqual(TEXT("under one step, nothing"), Under, 0.0f);
	TestEqual(TEXT("**one step, 1%**"), One, 0.01f, 0.0001f);
	TestEqual(TEXT("29 whole steps, 29%"), TwentyNine, 0.29f, 0.0001f);
	TestEqual(TEXT("**30 steps, 30%**"), Thirty, 0.30f, 0.0001f);
	TestEqual(TEXT("**and no more**"), Beyond, 0.30f, 0.0001f);
	TestEqual(TEXT("CONTROL: without the node, nothing"), Without, 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// G4. A plan charges the discounted price, at its material's tier, and does not refuse a player who can pay it.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Upgrading a +0 helm to +1 takes +1 stones, tier 1. With tier 1 at 0.80 its price is four fifths of the full one, and
 * a player holding exactly that is not refused, where without the pricing the same gold is refused (the discount is
 * applied before the gold refusal). CONTROL: a +4 helm's upgrade to +5 takes +5 stones, tier 3, and is at its full
 * price. And the default pricing, ruling A's, prices at full. Prints "PRICED PLAN".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesPlanTest,
	"Cataclysm.ForgeGoldNodes.APlanChargesTheDiscountedPriceAndDoesNotRefuseAPlayerWhoCanPayIt")
{
	using namespace CataclysmForgeGoldNodesTest;
	if (!TestNotNull(TEXT("set-up: the crafting material table"), UCataclysmDropRoll::LoadCraftingMaterialTable()))
	{
		return false;
	}
	const FCataclysmForgePricing Pricing = PricingWith(0.80f, 1.0f, 1.0f);
	const FCataclysmItem Low = UpgradeHelm(0, 0);
	const FCataclysmItem High = UpgradeHelm(4, 0);
	const FCataclysmForgeUpgrade FullLow = UCataclysmForge::UpgradeOf(Low, 2, 1, 1000000);
	const int64 Discounted = FMath::Max<int64>(1, FMath::RoundToInt64(static_cast<double>(FullLow.Gold) * 0.80));
	const FCataclysmForgeUpgrade PricedLow = UCataclysmForge::UpgradeOf(Low, 2, 1, Discounted, Pricing);
	const FCataclysmForgeUpgrade UnpricedLow = UCataclysmForge::UpgradeOf(Low, 2, 1, Discounted);
	const FCataclysmForgeUpgrade FullHigh = UCataclysmForge::UpgradeOf(High, 2, 1, 1000000);
	const FCataclysmForgeUpgrade PricedHigh = UCataclysmForge::UpgradeOf(High, 2, 1, 1000000, Pricing);
	AddInfo(FString::Printf(TEXT("PRICED PLAN: +0 to +1 with %s (tier %d): full %lld, priced %lld, possible %d holding "
								 "%lld; unpriced possible %d; +4 to +5 with %s (tier %d): full %lld, priced %lld"),
							*PricedLow.Stone.ToString(), Pricing.TierOf(PricedLow.Stone), FullLow.Gold, PricedLow.Gold,
							PricedLow.bPossible ? 1 : 0, Discounted, UnpricedLow.bPossible ? 1 : 0,
							*PricedHigh.Stone.ToString(), Pricing.TierOf(PricedHigh.Stone), FullHigh.Gold,
							PricedHigh.Gold));

	TestEqual(TEXT("set-up: the +1 stone is tier 1"), Pricing.TierOf(PricedLow.Stone), 1);
	TestEqual(TEXT("set-up: the +5 stone is tier 3"), Pricing.TierOf(PricedHigh.Stone), 3);
	TestTrue(TEXT("set-up: the full price is above 1"), FullLow.Gold > 1);
	TestEqual(TEXT("**the plan charges four fifths**"), PricedLow.Gold, Discounted);
	TestTrue(TEXT("**and a player holding exactly that is not refused**"), PricedLow.bPossible);
	TestFalse(TEXT("CONTROL: without the pricing, the same gold is refused"), UnpricedLow.bPossible);
	TestEqual(TEXT("CONTROL: a tier 3 stone's upgrade is at its full price"), PricedHigh.Gold, FullHigh.Gold);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// G5. An option's gold is discounted at its own material's tier (ruling B).
// --------------------------------------------------------------------------------------------------------------------
/**
 * A craft on a piece at 250 residue, with a Chaos Stabilizer (tier 3) and one Tainted Shard (tier 2). With tier 2 at
 * 0.50 and tier 3 at 0.80, the options' gold is the Stabilizer's at four fifths plus the Shard's at half, each rounded.
 * CONTROL: the default pricing gives the two at full. Prints "OPTIONS PRICED".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesOptionsTest,
	"Cataclysm.ForgeGoldNodes.AnOptionsGoldIsDiscountedAtItsOwnMaterialsTier")
{
	using namespace CataclysmForgeGoldNodesTest;
	if (!TestNotNull(TEXT("set-up: the crafting material table"), UCataclysmDropRoll::LoadCraftingMaterialTable()))
	{
		return false;
	}
	const float Before = 250.0f;
	const float After = 275.0f;
	const int32 Penalty = UCataclysmForge::PenaltyDaysFor(Before);
	const int32 Days = 3 + Penalty;
	FCataclysmForgeCraftOptions Stabilizer;
	Stabilizer.bChaosStabilizer = true;
	FCataclysmForgeCraftOptions Shard;
	Shard.TaintedShards = 1;
	FCataclysmForgeCraftOptions Both;
	Both.bChaosStabilizer = true;
	Both.TaintedShards = 1;
	const FCataclysmForgePricing Pricing = PricingWith(1.0f, 0.50f, 0.80f);
	const int64 StabilizerGold = UCataclysmForge::OptionsOn(Before, After, Days, 1000, Stabilizer, 1, 1, 1, 1000000).Gold;
	const int64 ShardGold = UCataclysmForge::OptionsOn(Before, After, Days, 1000, Shard, 1, 1, 1, 1000000).Gold;
	const FCataclysmForgeOptionEffect Full = UCataclysmForge::OptionsOn(Before, After, Days, 1000, Both, 1, 1, 1, 1000000);
	const FCataclysmForgeOptionEffect Priced =
		UCataclysmForge::OptionsOn(Before, After, Days, 1000, Both, 1, 1, 1, 1000000, Pricing);
	const int64 Expected = FMath::Max<int64>(1, FMath::RoundToInt64(static_cast<double>(StabilizerGold) * 0.80))
		+ FMath::Max<int64>(1, FMath::RoundToInt64(static_cast<double>(ShardGold) * 0.50));
	AddInfo(FString::Printf(TEXT("OPTIONS PRICED: penalty %d; the Stabilizer (tier %d) %lld, a Shard (tier %d) %lld; "
								 "both at full %lld, priced %lld, expected %lld"),
							Penalty, Pricing.TierOf(FName(UCataclysmForge::ChaosStabilizerRow)), StabilizerGold,
							Pricing.TierOf(FName(UCataclysmForge::TaintedShardRow)), ShardGold, Full.Gold, Priced.Gold,
							Expected));

	if (!TestTrue(TEXT("set-up: the residue has a penalty day for a Shard"), Penalty >= 1)
		|| !TestTrue(TEXT("set-up: both options are possible"), Full.bPossible && Priced.bPossible))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the Chaos Stabilizer is tier 3"), Pricing.TierOf(FName(UCataclysmForge::ChaosStabilizerRow)), 3);
	TestEqual(TEXT("set-up: the Tainted Shard is tier 2"), Pricing.TierOf(FName(UCataclysmForge::TaintedShardRow)), 2);
	TestEqual(TEXT("CONTROL: at full, the two together"), Full.Gold, StabilizerGold + ShardGold);
	TestEqual(TEXT("**each at its own tier: the Stabilizer at 0.8, the Shard at 0.5**"), Priced.Gold, Expected);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// G6. Forge Upgrades draws once for an item on a day, on a stream of its own.
// --------------------------------------------------------------------------------------------------------------------
/**
 * The same item on the same day of the same run gives the same seed; another day gives another. A chance of 1 always
 * lands and 0 never does. With a chance of 0.5, among the first 40 days at least one draw lands and one does not, so the
 * draw is read rather than fixed. Its salt is its own. Prints "FORGE UPGRADES".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesDrawTest,
	"Cataclysm.ForgeGoldNodes.ForgeUpgradesDrawsOnceForAnItemOnADay")
{
	using namespace CataclysmForgeGoldNodesTest;
	const FCataclysmItem Helm = UpgradeHelm(2, 7);
	const int32 Mask = 0x1234;
	const int32 First = UCataclysmForge::ForgeUpgradesSeed(Helm, Mask, 10);
	const int32 Again = UCataclysmForge::ForgeUpgradesSeed(UpgradeHelm(2, 7), Mask, 10);
	const int32 NextDay = UCataclysmForge::ForgeUpgradesSeed(Helm, Mask, 11);
	int32 Landed = 0;
	for (int32 Day = 1; Day <= 40; ++Day)
	{
		Landed += UCataclysmForge::ForgeUpgradesTenthOff(0.5f, UCataclysmForge::ForgeUpgradesSeed(Helm, Mask, Day)) ? 1 : 0;
	}
	const bool bCertain = UCataclysmForge::ForgeUpgradesTenthOff(1.0f, First);
	const bool bNever = UCataclysmForge::ForgeUpgradesTenthOff(0.0f, First);
	AddInfo(FString::Printf(TEXT("FORGE UPGRADES: seeds %d, again %d, next day %d; at 0.5, %d of 40 days landed; certain "
								 "%d, never %d; salt %08x"),
							First, Again, NextDay, Landed, bCertain ? 1 : 0, bNever ? 1 : 0,
							UCataclysmForge::ForgeUpgradesSalt));

	TestEqual(TEXT("**the same item and day draw the same**"), Again, First);
	TestNotEqual(TEXT("another day draws again"), NextDay, First);
	TestTrue(TEXT("a chance of 1 lands"), bCertain);
	TestFalse(TEXT("a chance of 0 does not"), bNever);
	TestTrue(TEXT("**at 0.5 some days land and some do not**"), Landed > 0 && Landed < 40);
	TestTrue(TEXT("its salt is not The Soul Forge's"), UCataclysmForge::ForgeUpgradesSalt != UCataclysmForge::SoulForgeSalt);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// G7. An upgrade with the nodes pays the discounted price and counts its tier 1 gold; a refused one counts nothing.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A solo character upgrades a helm from +2 to +3 (+3 stones, tier 2), whose full price is 4,500 gold (the milestones'
 * helm). Holding Workshop Upgrades at 8 and Apprentice Oversight it pays 32% less, 3,060, and its own forge count rises by
 * 3,060 over the run's gold scale. Without stones the upgrade is refused, and the count does not move. Holding Forge
 * Upgrades at 5 alone, a helm whose draw lands pays 4,050 and one whose draw does not pays 4,500. Prints "FORGE PAID".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesPaidTest,
	"Cataclysm.ForgeGoldNodes.AnUpgradeWithTheNodesPaysTheDiscountAndCountsItsTierOneGold")
{
	using namespace CataclysmForgeGoldNodesTest;
	FGoldNodesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestTrue(TEXT("set-up: the generated tables"), UCataclysmEmpireTree::LoadNodeTable()
					 && UCataclysmEmpireTree::LoadEffectTable() && UCataclysmDropRoll::LoadCraftingMaterialTable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const double Scale = FMath::Max(1.0e-6, static_cast<double>(UCataclysmDropRoll::GoldScaleFor(Scene.Run->DifficultyTier)));

	Scene.Hold({});
	const FPaid Control = UpgradeOnce(Scene, 0);
	const double CountAfterControl = Scene.State->GetOwnGoldSpentAtTheForge();

	Scene.Hold({{WorkshopUpgrades, 8}, {ApprenticeOversight, 1}});
	const FPaid Discounted = UpgradeOnce(Scene, 1);
	const FPaid Refused = UpgradeOnce(Scene, 2, /*bWithStones*/ false);

	Scene.Hold({{ForgeUpgrades, 5}});
	const int32 Mask = Scene.Run->WalkSeedMask;
	auto Lands = [&](int32 Kills)
	{
		return UCataclysmForge::ForgeUpgradesTenthOff(
			0.05f, UCataclysmForge::ForgeUpgradesSeed(UpgradeHelm(2, Kills), Mask, Scene.Run->Day()));
	};
	const int32 LandsKills = KillsWhere(Lands);
	const FPaid Tenth = LandsKills != INDEX_NONE ? UpgradeOnce(Scene, LandsKills) : FPaid();
	const int32 MissesKills = KillsWhere([&](int32 Kills) { return !Lands(Kills); });
	const FPaid NoTenth = MissesKills != INDEX_NONE ? UpgradeOnce(Scene, MissesKills) : FPaid();
	AddInfo(FString::Printf(TEXT("FORGE PAID: scale %.4f; no node %lld (count %.2f); Workshop and Oversight %lld (count "
								 "%.2f); refused %d (count %.2f); Forge Upgrades landing (kills %d) %lld, missing (kills "
								 "%d) %lld"),
							Scale, Control.Gold, CountAfterControl, Discounted.Gold, Discounted.CountAfter,
							static_cast<int32>(Refused.Result), Refused.CountAfter, LandsKills, Tenth.Gold, MissesKills,
							NoTenth.Gold));

	if (!TestTrue(TEXT("set-up: a helm whose draw lands and one whose draw misses"),
				  LandsKills != INDEX_NONE && MissesKills != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: with no node, 4,500 gold"), Control.Gold, static_cast<int64>(4500));
	TestEqual(TEXT("and the count rises by it, in tier 1 gold"), CountAfterControl, 4500.0 / Scale, 0.01);
	TestEqual(TEXT("**Workshop Upgrades and Apprentice Oversight take 32%: 3,060**"), Discounted.Gold,
			  static_cast<int64>(3060));
	TestEqual(TEXT("**the count rises by the price paid**"), Discounted.CountAfter - CountAfterControl, 3060.0 / Scale,
			  0.01);
	TestTrue(TEXT("set-up: without stones the upgrade is refused"), Refused.Result == ECataclysmForgeResult::Refused);
	TestEqual(TEXT("**a refused craft counts nothing**"), Refused.CountAfter, Discounted.CountAfter, 0.0001);
	TestEqual(TEXT("and charges nothing"), Refused.Gold, static_cast<int64>(0));
	TestEqual(TEXT("**Forge Upgrades' tenth: 4,050**"), Tenth.Gold, static_cast<int64>(4050));
	TestEqual(TEXT("CONTROL: a draw that misses pays 4,500"), NoTenth.Gold, static_cast<int64>(4500));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// G8. An account from before the forge count starts it at nothing; the committed account reads its count.
// --------------------------------------------------------------------------------------------------------------------
/**
 * `Account_v7.json` migrates (`Migrate_7_to_8`) with 0 tier 1 gold spent at the forge, keeping its 75,000.5 spent on
 * city upgrades. CONTROL: `Account_v8.json`, the committed account file since #47 slice F6a, is read rather than
 * migrated and states 62,500.25, a figure and not the default. Prints "FORGE COUNT FILES".
 */
CATACLYSM_GOLD_NODES_TEST(FCataclysmForgeGoldNodesFilesTest,
	"Cataclysm.ForgeGoldNodes.AnOlderAccountStartsTheForgeCountAtNothingAndTheCommittedOneReadsIt")
{
	using namespace CataclysmForgeGoldNodesTest;
	ECataclysmSaveLoadResult OldResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult NewResult = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmAccountSave* Old = ReadFixture<UCataclysmAccountSave>(*this, TEXT("Account_v7.json"), OldResult);
	const UCataclysmAccountSave* Committed = ReadFixture<UCataclysmAccountSave>(*this, TEXT("Account_v8.json"), NewResult);
	if (!Old || !Committed)
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("FORGE COUNT FILES: Account_v7.json %s, forge %.2f, city upgrades %.2f; Account_v8.json "
								 "%s, forge %.2f, city upgrades %.2f"),
							FCataclysmSaveStorage::Describe(OldResult), Old->GoldSpentAtTheForge,
							Old->GoldSpentOnCityUpgrades, FCataclysmSaveStorage::Describe(NewResult),
							Committed->GoldSpentAtTheForge, Committed->GoldSpentOnCityUpgrades));

	TestEqual(TEXT("Account_v7.json is migrated"), static_cast<int32>(OldResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("**with nothing spent at the forge**"), Old->GoldSpentAtTheForge, 0.0);
	TestEqual(TEXT("keeping its city upgrade count"), Old->GoldSpentOnCityUpgrades, 75000.5, 0.0001);
	TestEqual(TEXT("CONTROL: Account_v8.json is read, not migrated"), static_cast<int32>(NewResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("**and states its forge count**"), Committed->GoldSpentAtTheForge, 62500.25, 0.0001);
	return true;
}

#undef CATACLYSM_GOLD_NODES_TEST

#endif // WITH_AUTOMATION_TESTS
