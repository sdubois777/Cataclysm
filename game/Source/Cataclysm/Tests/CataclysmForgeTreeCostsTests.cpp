// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
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
#include "Tests/CataclysmTestWorld.h"

/**
 * THE EMPIRE TREE'S CRAFTING NODES ON A FORGE CRAFT'S RESIDUE AND DAYS. #2741's crafting group, slices C1 and C2, ruled
 * by the coordinating session on 2026-11-25 (G1 to G4 and every C1/C2 recommendation as written).
 *
 * Tool Maintenance keeps 0.95 a point of the residue a craft adds, Limited Knowledge 0.975 a point on an item below +6
 * before the craft, Heat Shielding 0.975 a point on a craft whose own material is tier 3 or above, each compounded per
 * point and multiplied; Stable Compounds takes 0.025 a point off the Chaos Stabilizer's 0.5; Master's Touch gives 1% for
 * each whole 100 of the residue before the craft that the craft leaves it at 0. The days, after the Tainted Shards: times
 * Industrial Speed's and Sovereign's Assembly's multipliers, less Master Craftsman's day a point, rounded; a craft of at
 * least one day keeps one, a craft of none stays at none.
 *
 * WRITTEN BEFORE A BUILD. The scene is `CataclysmForgeMaterialNodesTests.cpp`'s, copied into this file's namespace.
 */
namespace CataclysmForgeTreeCostsTest
{
	/** The eight nodes' ids in `docs/Empire_Development_Tree_Final.json`, and their full points. */
	const FName ToolMaintenance(TEXT("basic_arts_t1e"));		// 8 points
	const FName LimitedKnowledge(TEXT("basic_arts_t1g"));		// 10
	const FName HeatShielding(TEXT("basic_0000005f"));			// 5
	const FName StableCompounds(TEXT("basic_arts_t2f"));		// 10
	const FName MastersTouch(TEXT("keystone_0000005c"));		// 1
	const FName MasterCraftsman(TEXT("basic_00000057"));		// 10
	const FName IndustrialSpeed(TEXT("basic_00000053"));		// 5
	const FName SovereignsAssembly(TEXT("keystone_0000005d"));	// 1

	/** A helm at a gear level carrying `Residue`, with the kill count that picks its draws. */
	FCataclysmItem Helm(int32 GearLevel, int32 Kills, float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = GearLevel;
		Item.Residue = Residue;
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

	struct FCostsScene
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
	FCostsScene MakeScene()
	{
		FCostsScene Out;
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
			// SOLO SELF-FOUND: its empire tree and its gold are its own, on its player state.
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Artisan"), Partition);
			Out.State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2741, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}

	/** A costs struct with these figures and the rest at none. */
	FCataclysmForgeCosts CostsWith(float ResidueFactor, float StabilizerShareReduced, bool bReset, float DaysFactor,
								   float DaysOff)
	{
		FCataclysmForgeCosts Out;
		Out.ResidueFactor = ResidueFactor;
		Out.StabilizerShareReduced = StabilizerShareReduced;
		Out.bResidueReset = bReset;
		Out.DaysFactor = DaysFactor;
		Out.DaysOff = DaysOff;
		return Out;
	}
}

#define CATACLYSM_TREE_COSTS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// K1. The tree gives the eight nodes' figures.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Read through the generated tables, each node at its full points: Tool Maintenance 0.95 to the 8th (0.663), Limited
 * Knowledge 0.975 to the 10th (0.776), Heat Shielding 0.975 to the 5th (0.881), Stable Compounds 0.25, Master's Touch
 * 0.01, Master Craftsman 10 days, Industrial Speed 0.5, Sovereign's Assembly 0.5. CONTROL: none held gives the factors
 * at 1 and the rest at nought. Prints "TREE COSTS".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsTreeTest, "Cataclysm.ForgeTreeCosts.TheEightNodesGiveTheirFigures")
{
	using namespace CataclysmForgeTreeCostsTest;
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
	const FCataclysmEmpireTreePlayerBonuses All = With({{ToolMaintenance, 8}, {LimitedKnowledge, 10}, {HeatShielding, 5},
		{StableCompounds, 10}, {MastersTouch, 1}, {MasterCraftsman, 10}, {IndustrialSpeed, 5}, {SovereignsAssembly, 1}});
	AddInfo(FString::Printf(TEXT("TREE COSTS: all held: residue factors %.4f %.4f %.4f, Stabilizer share off %.3f, reset "
								 "%.3f a hundred, days off %.1f, reduced %.3f, more %.3f; none held %.2f %.2f %.2f %.2f "
								 "%.2f %.2f %.2f %.2f"),
							All.CraftResidueFactor, All.LowLevelCraftResidueFactor, All.RareMaterialCraftResidueFactor,
							All.StabilizerShareReduced, All.ResidueResetChancePerHundred, All.CraftDaysOff,
							All.CraftDaysReduced, All.CraftDaysReducedMore, None.CraftResidueFactor,
							None.LowLevelCraftResidueFactor, None.RareMaterialCraftResidueFactor,
							None.StabilizerShareReduced, None.ResidueResetChancePerHundred, None.CraftDaysOff,
							None.CraftDaysReduced, None.CraftDaysReducedMore));

	TestEqual(TEXT("**Tool Maintenance: 0.95 to the 8th**"), All.CraftResidueFactor, FMath::Pow(0.95f, 8.0f), 0.0001f);
	TestEqual(TEXT("**Limited Knowledge: 0.975 to the 10th**"), All.LowLevelCraftResidueFactor, FMath::Pow(0.975f, 10.0f),
			  0.0001f);
	TestEqual(TEXT("**Heat Shielding: 0.975 to the 5th**"), All.RareMaterialCraftResidueFactor, FMath::Pow(0.975f, 5.0f),
			  0.0001f);
	TestEqual(TEXT("Stable Compounds: 0.25 off the Stabilizer's share"), All.StabilizerShareReduced, 0.25f, 0.0001f);
	TestEqual(TEXT("Master's Touch: 1% a hundred"), All.ResidueResetChancePerHundred, 0.01f, 0.0001f);
	TestEqual(TEXT("Master Craftsman: 10 days"), All.CraftDaysOff, 10.0f, 0.0001f);
	TestEqual(TEXT("**Industrial Speed: 50% (10% a point, the ruling of 2026-11-25)**"), All.CraftDaysReduced, 0.5f,
			  0.0001f);
	TestEqual(TEXT("Sovereign's Assembly: 50%"), All.CraftDaysReducedMore, 0.5f, 0.0001f);
	TestTrue(TEXT("CONTROL: none held changes nothing"),
			 None.CraftResidueFactor == 1.0f && None.LowLevelCraftResidueFactor == 1.0f
				 && None.RareMaterialCraftResidueFactor == 1.0f && None.StabilizerShareReduced == 0.0f
				 && None.ResidueResetChancePerHundred == 0.0f && None.CraftDaysOff == 0.0f
				 && None.CraftDaysReduced == 0.0f && None.CraftDaysReducedMore == 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// K2. The residue: the tree's factor, Stable Compounds' share and Master's Touch's reset, apart from the options'.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A craft from 100 to 200 residue. With a factor of 0.5 it ends at 150, its options alone at 200. With a Chaos
 * Stabilizer and 0.25 off its share, the options alone end at 125 (a quarter kept), saving 75, and the factor of 0.5 then
 * takes it to 112.5. With Master's Touch's reset it ends at 0. CONTROL: the default costs leave the Stabilizer's half,
 * 150. Prints "RESIDUE".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsResidueTest,
	"Cataclysm.ForgeTreeCosts.TheResidueTakesTheTreesFactorTheStabilizersShareAndTheReset")
{
	using namespace CataclysmForgeTreeCostsTest;
	FCataclysmForgeCraftOptions Plain;
	FCataclysmForgeCraftOptions Stabilized;
	Stabilized.bChaosStabilizer = true;
	auto On = [](const FCataclysmForgeCraftOptions& Options, const FCataclysmForgeCosts& Costs)
	{
		return UCataclysmForge::OptionsOn(100.0f, 200.0f, 3, 0, Options, 1, 0, 1, 1000000, FCataclysmForgePricing(), Costs);
	};
	const FCataclysmForgeOptionEffect Halved = On(Plain, CostsWith(0.5f, 0.0f, false, 1.0f, 0.0f));
	const FCataclysmForgeOptionEffect Compounded = On(Stabilized, CostsWith(0.5f, 0.25f, false, 1.0f, 0.0f));
	const FCataclysmForgeOptionEffect Reset = On(Plain, CostsWith(1.0f, 0.0f, true, 1.0f, 0.0f));
	const FCataclysmForgeOptionEffect Default = On(Stabilized, FCataclysmForgeCosts());
	AddInfo(FString::Printf(TEXT("RESIDUE: factor 0.5: %.2f (options alone %.2f); Stabilizer at a quarter and factor 0.5: "
								 "%.2f (options alone %.2f, saved %.2f); reset %.2f; default Stabilizer %.2f"),
							Halved.ResidueAfter, Halved.OptionsResidueAfter, Compounded.ResidueAfter,
							Compounded.OptionsResidueAfter, Compounded.ResidueSaved, Reset.ResidueAfter,
							Default.ResidueAfter));

	TestEqual(TEXT("**the tree's factor takes what the craft adds: 150**"), Halved.ResidueAfter, 150.0f, 0.001f);
	TestEqual(TEXT("and the options alone leave 200"), Halved.OptionsResidueAfter, 200.0f, 0.001f);
	TestEqual(TEXT("**Stable Compounds: the Stabilizer keeps a quarter, 125**"), Compounded.OptionsResidueAfter, 125.0f,
			  0.001f);
	TestEqual(TEXT("saving 75"), Compounded.ResidueSaved, 75.0f, 0.001f);
	TestEqual(TEXT("**and the factor after it: 112.5**"), Compounded.ResidueAfter, 112.5f, 0.001f);
	TestTrue(TEXT("**Master's Touch's reset leaves 0**"), Reset.ResidueAfter == 0.0f && Reset.bResidueReset);
	TestEqual(TEXT("CONTROL: the default costs leave the Stabilizer's half"), Default.ResidueAfter, 150.0f, 0.001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// K3. The days: the multipliers, then the days off, then rounded; at least one, and none stays none; after the shards.
// --------------------------------------------------------------------------------------------------------------------
/**
 * 10 days times 0.475 (a multiplier of 0.95, then Sovereign's Assembly's 0.5) is 4.75, rounded 5; less 2 days off,
 * 2.75, rounded 3. 1 day less 10 keeps 1; 0 days stay 0; the default leaves 3 at 3. A craft of 6 days with a residue of
 * 250 (2 days of penalty) and 1 Tainted Shard has 5 with the options alone, and 2 days off then make 3. Prints "DAYS".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsDaysTest,
	"Cataclysm.ForgeTreeCosts.TheDaysTakeTheMultipliersThenTheDaysOffThenRoundAfterTheShards")
{
	using namespace CataclysmForgeTreeCostsTest;
	const int32 Multiplied = UCataclysmForge::TreeDaysFor(10, CostsWith(1.0f, 0.0f, false, 0.475f, 0.0f));
	const int32 ThenOff = UCataclysmForge::TreeDaysFor(10, CostsWith(1.0f, 0.0f, false, 0.475f, 2.0f));
	const int32 KeepsOne = UCataclysmForge::TreeDaysFor(1, CostsWith(1.0f, 0.0f, false, 1.0f, 10.0f));
	const int32 NoneStays = UCataclysmForge::TreeDaysFor(0, CostsWith(1.0f, 0.0f, false, 0.5f, 10.0f));
	const int32 Default = UCataclysmForge::TreeDaysFor(3, FCataclysmForgeCosts());
	FCataclysmForgeCraftOptions OneShard;
	OneShard.TaintedShards = 1;
	const FCataclysmForgeOptionEffect Sharded = UCataclysmForge::OptionsOn(
		250.0f, 260.0f, 6, 0, OneShard, 0, 1, 1, 1000000, FCataclysmForgePricing(), CostsWith(1.0f, 0.0f, false, 1.0f, 2.0f));
	AddInfo(FString::Printf(TEXT("DAYS: 10 at 0.475 %d, less 2 %d; 1 less 10 %d; 0 %d; default 3 %d; 6 with a shard: %d "
								 "alone, %d with 2 off (possible %s)"),
							Multiplied, ThenOff, KeepsOne, NoneStays, Default, Sharded.OptionsDays, Sharded.Days,
							Sharded.bPossible ? TEXT("yes") : TEXT("NO")));

	TestEqual(TEXT("**the multipliers first: 4.75 rounds to 5**"), Multiplied, 5);
	TestEqual(TEXT("**then the days off: 2.75 rounds to 3**"), ThenOff, 3);
	TestEqual(TEXT("**a craft of a day keeps one**"), KeepsOne, 1);
	TestEqual(TEXT("**a craft of none stays at none**"), NoneStays, 0);
	TestEqual(TEXT("CONTROL: the default leaves 3"), Default, 3);
	TestTrue(TEXT("set-up: the shard is possible"), Sharded.bPossible);
	TestEqual(TEXT("the shard alone: 5 days"), Sharded.OptionsDays, 5);
	TestEqual(TEXT("**then the tree's 2 days off: 3**"), Sharded.Days, 3);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// K4. Master's Touch counts whole hundreds of the residue before the craft.
// --------------------------------------------------------------------------------------------------------------------
/**
 * At 1% a hundred: 99 residue never lands (no whole hundred); 10,000 always lands (100%); 250 (2%) lands on some of 400
 * days and not on most. The same item on the same day draws the same. Prints "MASTER'S TOUCH".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsTouchTest,
	"Cataclysm.ForgeTreeCosts.MastersTouchCountsWholeHundredsOfTheResidueBeforeTheCraft")
{
	using namespace CataclysmForgeTreeCostsTest;
	int32 Low = 0;
	int32 Full = 0;
	int32 Some = 0;
	bool bSame = true;
	for (int32 Day = 1; Day <= 400; ++Day)
	{
		Low += UCataclysmForge::ResidueResetLands(Helm(2, 3, 99.0f), 0.01f, 0, Day) ? 1 : 0;
		Full += UCataclysmForge::ResidueResetLands(Helm(2, 3, 10000.0f), 0.01f, 0, Day) ? 1 : 0;
		const bool bLands = UCataclysmForge::ResidueResetLands(Helm(2, 3, 250.0f), 0.01f, 0, Day);
		Some += bLands ? 1 : 0;
		bSame = bSame && bLands == UCataclysmForge::ResidueResetLands(Helm(2, 3, 250.0f), 0.01f, 0, Day);
	}
	AddInfo(FString::Printf(TEXT("MASTER'S TOUCH: of 400 days, at 99 residue %d, at 10,000 %d, at 250 %d; the same twice "
								 "%s"),
							Low, Full, Some, bSame ? TEXT("yes") : TEXT("NO")));

	TestEqual(TEXT("**99 residue is no whole hundred: never**"), Low, 0);
	TestEqual(TEXT("**10,000 residue is 100%: always**"), Full, 400);
	TestTrue(TEXT("250 residue (2%) lands on some days and not on most"), Some > 0 && Some < 40);
	TestTrue(TEXT("the same item on the same day draws the same"), bSame);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// K5. In a world: an upgrade with Tool Maintenance and Master Craftsman takes their residue and days, as asked.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A +2 helm at 250 residue upgraded to +3 with Tool Maintenance at 8 and Master Craftsman at 10: it adds the plan's
 * residue times 0.95 to the 8th, and takes the plan's days less 10, at least one; the run moves by those days; the
 * question's words say both. CONTROL: with no node held the same upgrade takes the plan's residue and days. Prints
 * "UPGRADE COSTS".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsUpgradeTest,
	"Cataclysm.ForgeTreeCosts.AnUpgradeWithTheNodesTakesTheirResidueAndDaysAsTheQuestionSays")
{
	using namespace CataclysmForgeTreeCostsTest;
	FCostsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestTrue(TEXT("set-up: the empire tree's tables"),
				  UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	const FName Stone3 = UCataclysmForge::StoneFor(3);
	struct FUpgraded
	{
		bool bDone = false;
		float PlanAdded = 0.0f;
		int32 PlanDays = 0;
		float ResidueAfter = 0.0f;
		int32 DaysPassed = 0;
		FString Words;
	};
	auto UpgradeOnce = [&]()
	{
		FUpgraded Out;
		const int32 Cell = Scene.Bag()->AddItem(Helm(2, 11, 250.0f));
		Scene.Bag()->AddMaterial(Stone3, 2);
		const FCataclysmForgeTarget Target = FCataclysmForgeTarget::Carried(Cell);
		const FCataclysmForgeUpgrade Plan = UCataclysmForgeCraft::PlanFor(Scene.World, Target);
		Out.PlanAdded = Plan.ResidueAfter - Plan.ResidueBefore;
		Out.PlanDays = Plan.Days;
		const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
			Scene.World, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, FCataclysmForgeCraftOptions(),
			UCataclysmForgeCraft::PricingFor(Scene.World, Target),
			UCataclysmForgeCraft::CostsFor(Scene.World, Target, Plan.Stone));
		Out.Words = UCataclysmForge::CostsText(Effect, Plan.ResidueBefore);
		const int32 DayBefore = Scene.Run->Day();
		FCataclysmForgeUpgrade Done;
		FString Message;
		Out.bDone = UCataclysmForgeCraft::Upgrade(Scene.World, Target, false, Done, Message) == ECataclysmForgeResult::Done;
		Out.ResidueAfter = Scene.Bag()->ItemAt(Cell) ? Scene.Bag()->ItemAt(Cell)->Residue : -1.0f;
		Out.DaysPassed = Scene.Run->Day() - DayBefore;
		if (Scene.Bag()->ItemAt(Cell))
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return Out;
	};
	const FUpgraded Bare = UpgradeOnce();
	Scene.Hold({{ToolMaintenance, 8}, {MasterCraftsman, 10}});
	const FUpgraded Tended = UpgradeOnce();
	const float Factor = FMath::Pow(0.95f, 8.0f);
	const int32 TendedDays = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Tended.PlanDays) - 10.0f));
	AddInfo(FString::Printf(TEXT("UPGRADE COSTS: bare: added %.2f, %d days, residue %.2f, %d days passed; tended: added "
								 "%.2f, %d days, residue %.2f, %d days passed, asked \"%s\""),
							Bare.PlanAdded, Bare.PlanDays, Bare.ResidueAfter, Bare.DaysPassed, Tended.PlanAdded,
							Tended.PlanDays, Tended.ResidueAfter, Tended.DaysPassed, *Tended.Words));

	TestTrue(TEXT("set-up: both upgrades are done"), Bare.bDone && Tended.bDone);
	TestTrue(TEXT("set-up: the plan adds residue and takes more than one day"), Tended.PlanAdded > 0.0f && Tended.PlanDays > 1);
	TestEqual(TEXT("CONTROL: with no node the plan's residue"), Bare.ResidueAfter, 250.0f + Bare.PlanAdded, 0.01f);
	TestEqual(TEXT("CONTROL: and the plan's days"), Bare.DaysPassed, Bare.PlanDays);
	TestEqual(TEXT("**Tool Maintenance: the plan's residue times 0.95 to the 8th**"), Tended.ResidueAfter,
			  250.0f + Tended.PlanAdded * Factor, 0.01f);
	TestEqual(TEXT("**Master Craftsman: the plan's days less 10, at least one**"), Tended.DaysPassed, TendedDays);
	TestTrue(TEXT("the question says both"), Tended.Words.StartsWith(TEXT(" The empire tree's crafting nodes: residue 250 to "))
		&& Tended.Words.Contains(FString::Printf(TEXT("%d day%s, not %d"), TendedDays, TendedDays == 1 ? TEXT("") : TEXT("s"),
												Tended.PlanDays)));
	TestEqual(TEXT("CONTROL: with no node the question says nothing more"), Bare.Words, FString());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// K6. Limited Knowledge below +6 before the craft; Heat Shielding on a tier 3+ craft material only.
// --------------------------------------------------------------------------------------------------------------------
/**
 * With Limited Knowledge at 10 and Heat Shielding at 5 held, the costs for: a +5 helm's upgrade (to +6) with its +6 stone
 * (tier 3) takes both (0.776 times 0.881); a +6 helm's with its +7 stone (tier 4) only Heat Shielding's; a +2 helm's
 * with its +3 stone (tier 2) only Limited Knowledge's. CONTROL: a +2 helm's whose craft material is tier 2 with a Chaos
 * Stabilizer (tier 3) in the bag still takes no Heat Shielding: the options do not decide it. Prints "WHICH APPLY".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsWhichTest,
	"Cataclysm.ForgeTreeCosts.LimitedKnowledgeIsBelowSixAndHeatShieldingIsTheCraftsOwnMaterial")
{
	using namespace CataclysmForgeTreeCostsTest;
	FCostsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestTrue(TEXT("set-up: the tables"), UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable()
					  && UCataclysmDropRoll::LoadCraftingMaterialTable()))
	{
		return false;
	}
	Scene.Hold({{LimitedKnowledge, 10}, {HeatShielding, 5}});
	Scene.Bag()->AddMaterial(FName(UCataclysmForge::ChaosStabilizerRow), 1);
	auto FactorFor = [&](int32 GearLevel)
	{
		const int32 Cell = Scene.Bag()->AddItem(Helm(GearLevel, 4, 100.0f));
		const float Factor = UCataclysmForgeCraft::CostsFor(Scene.World, FCataclysmForgeTarget::Carried(Cell),
															UCataclysmForge::StoneFor(GearLevel + 1)).ResidueFactor;
		Scene.Bag()->RemoveItemAt(Cell);
		return Factor;
	};
	const float FiveToSix = FactorFor(5);
	const float SixToSeven = FactorFor(6);
	const float TwoToThree = FactorFor(2);
	const float Low = FMath::Pow(0.975f, 10.0f);
	const float Heat = FMath::Pow(0.975f, 5.0f);
	AddInfo(FString::Printf(TEXT("WHICH APPLY: +5 to +6 %.4f, +6 to +7 %.4f, +2 to +3 %.4f (Limited Knowledge %.4f, Heat "
								 "Shielding %.4f)"),
							FiveToSix, SixToSeven, TwoToThree, Low, Heat));

	TestEqual(TEXT("**+5 to +6 with a tier 3 stone: both**"), FiveToSix, Low * Heat, 0.0001f);
	TestEqual(TEXT("**+6 to +7 with a tier 4 stone: Heat Shielding only (not below +6)**"), SixToSeven, Heat, 0.0001f);
	TestEqual(TEXT("**+2 to +3 with a tier 2 stone: Limited Knowledge only, the Stabilizer in the bag not counted**"),
			  TwoToThree, Low, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// K7. The cleanse takes the day nodes and Master's Touch.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A helm at 1,200 residue: its cleanse takes 3 days and halves it to 600. With Master Craftsman at 10 it takes 1 day; with
 * Master's Touch held (12% at 1,200) on a helm whose draw lands it ends at 0, and the cleanse's words say both. CONTROL:
 * no node held, 3 days and 600. Prints "CLEANSE COSTS".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsCleanseTest,
	"Cataclysm.ForgeTreeCosts.TheCleanseTakesTheDayNodesAndMastersTouch")
{
	using namespace CataclysmForgeTreeCostsTest;
	FCostsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestTrue(TEXT("set-up: the empire tree's tables"),
				  UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	auto PlanOn = [&](int32 Kills)
	{
		Scene.Bag()->AddMaterial(FName(UCataclysmForge::PurifiedEssenceRow), 1);
		const int32 Cell = Scene.Bag()->AddItem(Helm(2, Kills, 1200.0f));
		const FCataclysmForgeCleanse Plan = UCataclysmForgeCraft::CleansePlanFor(Scene.World, FCataclysmForgeTarget::Carried(Cell));
		Scene.Bag()->RemoveItemAt(Cell);
		return Plan;
	};
	const FCataclysmForgeCleanse Bare = PlanOn(5);
	Scene.Hold({{MasterCraftsman, 10}, {MastersTouch, 1}});
	auto Lands = [&](int32 Kills)
	{
		return UCataclysmForge::ResidueResetLands(Helm(2, Kills, 1200.0f), 0.01f, Scene.Run->WalkSeedMask, Scene.Run->Day());
	};
	const int32 Kills = KillsWhere(Lands);
	if (!TestTrue(TEXT("set-up: a helm whose Master's Touch draw lands"), Kills != INDEX_NONE))
	{
		return false;
	}
	const FCataclysmForgeCleanse Touched = PlanOn(Kills);
	AddInfo(FString::Printf(TEXT("CLEANSE COSTS: bare %d days, residue %.1f; with the nodes (kills %d) %d days, residue %.1f, "
								 "\"%s\""),
							Bare.Days, Bare.ResidueAfter, Kills, Touched.Days, Touched.ResidueAfter,
							*UCataclysmForge::CleanseCostsText(Touched)));

	TestEqual(TEXT("CONTROL: no node: 3 days"), Bare.Days, 3);
	TestEqual(TEXT("CONTROL: and half the residue"), Bare.ResidueAfter, 600.0f, 0.01f);
	TestEqual(TEXT("**Master Craftsman: 1 day**"), Touched.Days, 1);
	TestTrue(TEXT("**Master's Touch: residue 0**"), Touched.ResidueAfter == 0.0f && Touched.bResidueReset);
	TestEqual(TEXT("the cleanse's words say both"), UCataclysmForge::CleanseCostsText(Touched),
			  FString(TEXT(" Master's Touch and the empire tree's crafting nodes: residue 1200 to 0, not 600; 1 day, not 3.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// K8. The words: what the tree changed, after the options' sentences; nothing when nothing changed.
// --------------------------------------------------------------------------------------------------------------------
/**
 * An effect whose options alone leave 275 and 5 days and whose tree leaves 266.6 and 1 day says " The empire tree's
 * crafting nodes: residue 250 to 266.6, not 275; 1 day, not 5." Only the days changed, only the days are said. CONTROL:
 * nothing changed, nothing said. Prints "WORDS".
 */
CATACLYSM_TREE_COSTS_TEST(FCataclysmForgeTreeCostsWordsTest,
	"Cataclysm.ForgeTreeCosts.TheWordsSayWhatTheTreeChangedAfterTheOptions")
{
	using namespace CataclysmForgeTreeCostsTest;
	FCataclysmForgeOptionEffect Both;
	Both.OptionsResidueAfter = 275.0f;
	Both.ResidueAfter = 266.6f;
	Both.OptionsDays = 5;
	Both.Days = 1;
	FCataclysmForgeOptionEffect DaysOnly = Both;
	DaysOnly.ResidueAfter = 275.0f;
	DaysOnly.Days = 3;
	FCataclysmForgeOptionEffect Nothing = Both;
	Nothing.ResidueAfter = 275.0f;
	Nothing.Days = 5;
	AddInfo(FString::Printf(TEXT("WORDS: \"%s\"; \"%s\"; \"%s\""), *UCataclysmForge::CostsText(Both, 250.0f),
							*UCataclysmForge::CostsText(DaysOnly, 250.0f), *UCataclysmForge::CostsText(Nothing, 250.0f)));

	TestEqual(TEXT("**both said**"), UCataclysmForge::CostsText(Both, 250.0f),
			  FString(TEXT(" The empire tree's crafting nodes: residue 250 to 266.6, not 275; 1 day, not 5.")));
	TestEqual(TEXT("only the days"), UCataclysmForge::CostsText(DaysOnly, 250.0f),
			  FString(TEXT(" The empire tree's crafting nodes: 3 days, not 5.")));
	TestEqual(TEXT("CONTROL: nothing changed, nothing said"), UCataclysmForge::CostsText(Nothing, 250.0f), FString());
	return true;
}

#undef CATACLYSM_TREE_COSTS_TEST

#endif // WITH_AUTOMATION_TESTS
