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
#include "Player/CataclysmPlayLoopLog.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

/**
 * THE EMPIRE TREE'S THREE FORGE MATERIAL NODES. Issue #47, slice F6b, ruled by the coordinating session on 2026-11-22
 * (Q11 to Q17) and 2026-11-25 (N1 to N5).
 *
 * Material Conservation, 2% a point, keeps a forge item craft's tier 1 and 2 materials; Resource Management, 1% a point,
 * its tier 3 to 5 ones ("Rare" is tier 3 and above, Q14); Tempting Fate, 15%, every material, on an item whose residue
 * before the craft is above 500 (Q15, N5). Each drawn once a craft from the item and the day (Q12, Q13), independently
 * (Q16); the option materials count by their tier (Q17); a gem is not a crafting material (N2); the combines never keep
 * (Q11); the cleanse counts (N3). The craft takes everything and puts the kept materials back once it can no longer fail
 * (N1). The question names what will be kept, and a PLAY LOOP line what was (N4).
 *
 * WRITTEN BEFORE A BUILD. The scene is `CataclysmForgeGoldNodesTests.cpp`'s, copied into this file's namespace.
 */
namespace CataclysmForgeMaterialNodesTest
{
	/** The three nodes' ids in `docs/Empire_Development_Tree_Final.json`, and their full points. */
	const FName MaterialConservation(TEXT("basic_00000050"));	// 10 points
	const FName ResourceManagement(TEXT("basic_00000058"));		// 20
	const FName TemptingFate(TEXT("keystone_cLiRR6p7B7"));		// 1

	/** Materials of each tier, from `game/Data/CraftingMaterials.csv`. */
	const FName CorruptedMote(TEXT("Material_Corrupted_Mote"));	// tier 1
	const FName PrimalSpark(TEXT("Material_Primal_Spark"));		// tier 5

	/** A helm at a gear level carrying `Residue`, with the kill count that picks its draws (the gold nodes' helm). */
	FCataclysmItem UpgradeHelm(int32 GearLevel, int32 Kills, float Residue = 250.0f)
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

	struct FMaterialNodesScene
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

		/** The three draws for `Item` today, at these chances, as the craft draws them. */
		FCataclysmForgeKeeping Draws(const FCataclysmItem& Item, float Low, float Rare, float All) const
		{
			return UCataclysmForge::KeepingOn(Item, Low, Rare, All, Run->WalkSeedMask, Run->Day(),
											  UCataclysmDropRoll::LoadCraftingMaterialTable());
		}
	};

	/** A world with this project's game instance, a possessed solo player with a million gold of its own, and a run. */
	FMaterialNodesScene MakeScene()
	{
		FMaterialNodesScene Out;
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
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Out.Run->Begin(/* InSeed */ 4762, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}
}

#define CATACLYSM_MATERIAL_NODES_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// M1. The tree gives the three nodes' chances.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Read through the generated tables: Material Conservation at 10 points gives 20% and at 3 points 6%, Resource Management
 * at 20 points 20%, Tempting Fate 15%. CONTROL: no node held gives nought. Prints "MATERIAL NODES".
 */
CATACLYSM_MATERIAL_NODES_TEST(FCataclysmForgeMaterialNodesTreeTest,
	"Cataclysm.ForgeMaterialNodes.TheThreeNodesGiveTheirChances")
{
	using namespace CataclysmForgeMaterialNodesTest;
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
	const FCataclysmEmpireTreePlayerBonuses All = With({{MaterialConservation, 10}, {ResourceManagement, 20},
		{TemptingFate, 1}});
	const FCataclysmEmpireTreePlayerBonuses ThreePoints = With({{MaterialConservation, 3}});
	AddInfo(FString::Printf(TEXT("MATERIAL NODES: all held: low tier %.3f, rare %.3f, returned %.3f; Material "
								 "Conservation at 3 points %.3f; none held %.3f %.3f %.3f"),
							All.LowTierMaterialKeptChance, All.RareMaterialKeptChance,
							All.HighResidueMaterialsReturnedChance, ThreePoints.LowTierMaterialKeptChance,
							None.LowTierMaterialKeptChance, None.RareMaterialKeptChance,
							None.HighResidueMaterialsReturnedChance));

	TestEqual(TEXT("**Material Conservation at 10 points: 20%**"), All.LowTierMaterialKeptChance, 0.20f, 0.0001f);
	TestEqual(TEXT("and 2% a point: 6% at 3"), ThreePoints.LowTierMaterialKeptChance, 0.06f, 0.0001f);
	TestEqual(TEXT("**Resource Management at 20 points: 20%**"), All.RareMaterialKeptChance, 0.20f, 0.0001f);
	TestEqual(TEXT("**Tempting Fate: 15%**"), All.HighResidueMaterialsReturnedChance, 0.15f, 0.0001f);
	TestTrue(TEXT("CONTROL: none held gives nought"), None.LowTierMaterialKeptChance == 0.0f
				 && None.RareMaterialKeptChance == 0.0f && None.HighResidueMaterialsReturnedChance == 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M2. Each node keeps its own tiers, and the words name the node.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A craft's materials: 2 Upgrade Stone +3 (tier 2), 1 Chaos Stabilizer (tier 3, an option, Q17), 1 Corrupted Mote
 * (tier 1). Material Conservation's draw keeps the stones and the Mote; Resource Management's the Stabilizer; both
 * together all three, each named by its node; Tempting Fate's all three, named by it alone (N4). A tier 5 Primal Spark is
 * Resource Management's. A name the crafting material table does not hold (as a gem's, N2) is kept by neither chance
 * node. CONTROL: no draw keeps nothing and says nothing. The PLAY LOOP line's words. Prints "KEPT".
 */
CATACLYSM_MATERIAL_NODES_TEST(FCataclysmForgeMaterialNodesTiersTest,
	"Cataclysm.ForgeMaterialNodes.EachNodeKeepsItsOwnTiersAndTheWordsNameIt")
{
	using namespace CataclysmForgeMaterialNodesTest;
	const UDataTable* Table = UCataclysmDropRoll::LoadCraftingMaterialTable();
	if (!TestNotNull(TEXT("set-up: the crafting material table"), Table))
	{
		return false;
	}
	const FName Stone3 = UCataclysmForge::StoneFor(3);
	const FName Stabilizer(UCataclysmForge::ChaosStabilizerRow);
	const TArray<FCataclysmForgeMaterialTake> Materials = {{Stone3, 2}, {Stabilizer, 1}, {CorruptedMote, 1}};
	auto Keeping = [Table](bool bLow, bool bRare, bool bAll)
	{
		FCataclysmForgeKeeping Out;
		Out.bLowTierKept = bLow;
		Out.bRareKept = bRare;
		Out.bAllReturned = bAll;
		Out.MaterialTable = Table;
		return Out;
	};
	const FCataclysmForgeKeeping Low = Keeping(true, false, false);
	const FCataclysmForgeKeeping Rare = Keeping(false, true, false);
	const FCataclysmForgeKeeping Both = Keeping(true, true, false);
	const FCataclysmForgeKeeping Fate = Keeping(true, false, true);
	const FCataclysmForgeKeeping None = Keeping(false, false, false);
	const FString Line = FCataclysmPlayLoopLog::ForgeMaterialsKeptLine(TEXT("Quality Helm"), Low.KeptPartsOf({{Stone3, 2}}));
	AddInfo(FString::Printf(TEXT("KEPT: low \"%s\"; rare \"%s\"; both \"%s\"; fate \"%s\"; none \"%s\"; line \"%s\""),
							*Low.KeptPartsOf(Materials), *Rare.KeptPartsOf(Materials), *Both.KeptPartsOf(Materials),
							*Fate.KeptPartsOf(Materials), *None.KeptPartsOf(Materials), *Line));

	TestEqual(TEXT("**Material Conservation keeps the tier 1 and 2 materials**"), Low.KeptPartsOf(Materials),
			  FString(TEXT("2 Upgrade Stone +3 (Material Conservation), 1 Corrupted Mote (Material Conservation)")));
	TestEqual(TEXT("**Resource Management the tier 3 one, the option's Stabilizer**"), Rare.KeptPartsOf(Materials),
			  FString(TEXT("1 Chaos Stabilizer (Resource Management)")));
	TestEqual(TEXT("both together keep all three, each named by its node"), Both.KeptPartsOf(Materials),
			  FString(TEXT("2 Upgrade Stone +3 (Material Conservation), 1 Chaos Stabilizer (Resource Management), 1 "
						   "Corrupted Mote (Material Conservation)")));
	TestEqual(TEXT("**Tempting Fate returns all three, named alone**"), Fate.KeptPartsOf(Materials),
			  FString(TEXT("2 Upgrade Stone +3 (Tempting Fate), 1 Chaos Stabilizer (Tempting Fate), 1 Corrupted Mote "
						   "(Tempting Fate)")));
	TestEqual(TEXT("a whole take is kept, both stones (Q12)"), Low.KeptOf(Materials).Num() > 0
				  ? Low.KeptOf(Materials)[0].Count : 0, 2);
	TestEqual(TEXT("a tier 5 Primal Spark is Resource Management's"), Rare.NodeFor(PrimalSpark),
			  FString(TEXT("Resource Management")));
	TestTrue(TEXT("and not Material Conservation's"), Low.NodeFor(PrimalSpark).IsEmpty());
	TestTrue(TEXT("a name the material table does not hold is kept by neither chance node (N2)"),
			 Both.NodeFor(FName(TEXT("Gem_Not_A_Material"))).IsEmpty());
	TestTrue(TEXT("CONTROL: no draw keeps nothing"), None.KeptOf(Materials).IsEmpty());
	TestEqual(TEXT("and says nothing"), UCataclysmForge::KeptText(None, Materials), FString());
	TestEqual(TEXT("the question's words"), UCataclysmForge::KeptText(Low, {{Stone3, 2}}),
			  FString(TEXT(" The empire tree keeps 2 Upgrade Stone +3 (Material Conservation).")));
	TestEqual(TEXT("the PLAY LOOP line's words"), Line,
			  FString(TEXT("the empire tree kept 2 Upgrade Stone +3 (Material Conservation) at the forge on Quality Helm")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M3. The three draws: seeded by the item and the day, each its own, Tempting Fate only above 500.
// --------------------------------------------------------------------------------------------------------------------
/**
 * At a chance of 1 the two chance nodes land, and Tempting Fate lands on an item at 500.5 residue but not at 500 (N5);
 * at a chance of nought none lands, even at 900. The same item on the same day draws the same. Over 40 days at a half,
 * each node lands on some days and not on others, and Material Conservation and Resource Management differ on some day,
 * so their draws are their own (Q16). Prints "DRAWS".
 */
CATACLYSM_MATERIAL_NODES_TEST(FCataclysmForgeMaterialNodesDrawsTest,
	"Cataclysm.ForgeMaterialNodes.TheDrawsAreTheItemsAndTheDaysAndEachIsItsOwn")
{
	using namespace CataclysmForgeMaterialNodesTest;
	const UDataTable* Table = UCataclysmDropRoll::LoadCraftingMaterialTable();
	const FCataclysmItem Helm = UpgradeHelm(2, 7);
	const FCataclysmForgeKeeping Sure = UCataclysmForge::KeepingOn(Helm, 1.0f, 1.0f, 1.0f, 0, 5, Table);
	const FCataclysmForgeKeeping AtFiveHundred =
		UCataclysmForge::KeepingOn(UpgradeHelm(2, 7, 500.0f), 1.0f, 1.0f, 1.0f, 0, 5, Table);
	const FCataclysmForgeKeeping AboveFiveHundred =
		UCataclysmForge::KeepingOn(UpgradeHelm(2, 7, 500.5f), 1.0f, 1.0f, 1.0f, 0, 5, Table);
	const FCataclysmForgeKeeping Never = UCataclysmForge::KeepingOn(UpgradeHelm(2, 7, 900.0f), 0.0f, 0.0f, 0.0f, 0, 5, Table);
	int32 LowLands = 0;
	int32 RareLands = 0;
	int32 FateLands = 0;
	int32 Differ = 0;
	bool bSame = true;
	const FCataclysmItem Heavy = UpgradeHelm(2, 7, 900.0f);
	for (int32 Day = 1; Day <= 40; ++Day)
	{
		const FCataclysmForgeKeeping Draw = UCataclysmForge::KeepingOn(Heavy, 0.5f, 0.5f, 0.5f, 0, Day, Table);
		const FCataclysmForgeKeeping Again = UCataclysmForge::KeepingOn(Heavy, 0.5f, 0.5f, 0.5f, 0, Day, Table);
		LowLands += Draw.bLowTierKept ? 1 : 0;
		RareLands += Draw.bRareKept ? 1 : 0;
		FateLands += Draw.bAllReturned ? 1 : 0;
		Differ += Draw.bLowTierKept != Draw.bRareKept ? 1 : 0;
		bSame = bSame && Draw.bLowTierKept == Again.bLowTierKept && Draw.bRareKept == Again.bRareKept
			&& Draw.bAllReturned == Again.bAllReturned;
	}
	AddInfo(FString::Printf(TEXT("DRAWS: sure %d %d %d; at 500 fate %d, at 500.5 %d; never %d %d %d; over 40 days at a "
								 "half: low %d, rare %d, fate %d, low and rare differ on %d; the same twice %s"),
							Sure.bLowTierKept, Sure.bRareKept, Sure.bAllReturned, AtFiveHundred.bAllReturned,
							AboveFiveHundred.bAllReturned, Never.bLowTierKept, Never.bRareKept, Never.bAllReturned,
							LowLands, RareLands, FateLands, Differ, bSame ? TEXT("yes") : TEXT("NO")));

	TestTrue(TEXT("at a chance of 1 both chance nodes land"), Sure.bLowTierKept && Sure.bRareKept);
	TestFalse(TEXT("but not Tempting Fate on an item at 250 residue"), Sure.bAllReturned);
	TestFalse(TEXT("**nor at exactly 500: 'above' is more than (N5)**"), AtFiveHundred.bAllReturned);
	TestTrue(TEXT("**at 500.5 it lands**"), AboveFiveHundred.bAllReturned);
	TestTrue(TEXT("CONTROL: at a chance of nought none lands, even at 900"),
			 !Never.bLowTierKept && !Never.bRareKept && !Never.bAllReturned);
	TestTrue(TEXT("the same item on the same day draws the same"), bSame);
	TestTrue(TEXT("each lands on some of 40 days and not on others"), LowLands > 0 && LowLands < 40 && RareLands > 0
				 && RareLands < 40 && FateLands > 0 && FateLands < 40);
	TestTrue(TEXT("**Material Conservation's and Resource Management's draws are their own (Q16)**"), Differ > 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M4. An upgrade whose Material Conservation draw lands keeps its two stones, and the question said so.
// --------------------------------------------------------------------------------------------------------------------
/**
 * In a world, with Material Conservation at 10 points (20%): a +2 helm whose draw lands is upgraded to +3 and the two
 * Upgrade Stone +3 it took (tier 2) are back in the bag; the question for it named them. CONTROL: a helm whose draw
 * misses is upgraded and its two stones are gone, and its question named nothing. Prints "UPGRADE KEPT".
 */
CATACLYSM_MATERIAL_NODES_TEST(FCataclysmForgeMaterialNodesUpgradeTest,
	"Cataclysm.ForgeMaterialNodes.AnUpgradeWhoseDrawLandsKeepsItsStonesAndTheQuestionSaidSo")
{
	using namespace CataclysmForgeMaterialNodesTest;
	FMaterialNodesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestNotNull(TEXT("set-up: the crafting material table"), UCataclysmDropRoll::LoadCraftingMaterialTable())
		|| !TestTrue(TEXT("set-up: the empire tree's tables"),
					 UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	Scene.Hold({{MaterialConservation, 10}});
	const FName Stone3 = UCataclysmForge::StoneFor(3);
	auto Lands = [&](int32 Kills) { return Scene.Draws(UpgradeHelm(2, Kills), 0.20f, 0.0f, 0.0f).bLowTierKept; };
	// EACH SEARCHED ON THE DAY IT IS CRAFTED: the first upgrade's days move the day the second draws on.
	const int32 LandsKills = KillsWhere(Lands);
	if (!TestTrue(TEXT("set-up: a helm whose draw lands"), LandsKills != INDEX_NONE))
	{
		return false;
	}
	auto UpgradeOnce = [&](int32 Kills, FString& OutQuestionWords, int32& OutStonesAfter)
	{
		Scene.Bag()->RemoveMaterial(Stone3, Scene.Bag()->CountOfMaterial(Stone3));
		const int32 Cell = Scene.Bag()->AddItem(UpgradeHelm(2, Kills));
		Scene.Bag()->AddMaterial(Stone3, 2);
		const FCataclysmForgeTarget Target = FCataclysmForgeTarget::Carried(Cell);
		OutQuestionWords = UCataclysmForgeCraft::KeptTextFor(Scene.World, Target, Stone3, 2, {});
		FCataclysmForgeUpgrade Done;
		FString Message;
		const ECataclysmForgeResult Result = UCataclysmForgeCraft::Upgrade(Scene.World, Target, false, Done, Message);
		OutStonesAfter = Scene.Bag()->CountOfMaterial(Stone3);
		const int32 Level = Scene.Bag()->ItemAt(Cell) ? Scene.Bag()->ItemAt(Cell)->GearLevel : -1;
		if (Scene.Bag()->ItemAt(Cell))
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return Result == ECataclysmForgeResult::Done && Level == 3;
	};
	FString KeptWords;
	FString MissedWords;
	int32 KeptStones = -1;
	int32 MissedStones = -1;
	const bool bKeptDone = UpgradeOnce(LandsKills, KeptWords, KeptStones);
	const int32 MissesKills = KillsWhere([&](int32 Kills) { return !Lands(Kills); });
	if (!TestTrue(TEXT("set-up: a helm whose draw misses, on the day after the first"), MissesKills != INDEX_NONE))
	{
		return false;
	}
	const bool bMissedDone = UpgradeOnce(MissesKills, MissedWords, MissedStones);
	AddInfo(FString::Printf(TEXT("UPGRADE KEPT: landing (kills %d) done %s, %d stones after, asked \"%s\"; missing (kills "
								 "%d) done %s, %d stones after, asked \"%s\""),
							LandsKills, bKeptDone ? TEXT("yes") : TEXT("NO"), KeptStones, *KeptWords, MissesKills,
							bMissedDone ? TEXT("yes") : TEXT("NO"), MissedStones, *MissedWords));

	TestTrue(TEXT("the upgrade whose draw lands is done, +2 to +3"), bKeptDone);
	TestEqual(TEXT("**and its two stones are back in the bag**"), KeptStones, 2);
	TestEqual(TEXT("**its question named them**"), KeptWords,
			  FString(TEXT(" The empire tree keeps 2 Upgrade Stone +3 (Material Conservation).")));
	TestTrue(TEXT("CONTROL: the upgrade whose draw misses is done"), bMissedDone);
	TestEqual(TEXT("CONTROL: and its two stones are gone"), MissedStones, 0);
	TestEqual(TEXT("CONTROL: and its question named nothing"), MissedWords, FString());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M5. Tempting Fate returns every material above 500 residue, the option's Stabilizer among them.
// --------------------------------------------------------------------------------------------------------------------
/**
 * In a world, with Tempting Fate held: a +2 helm at 501 residue whose draw lands, upgraded with a Chaos Stabilizer,
 * ends with its two Upgrade Stone +3 and its Stabilizer back in the bag (Q17). CONTROL: the same draw on a helm at 400
 * residue returns nothing: its stones and Stabilizer are gone. Prints "FATE".
 */
CATACLYSM_MATERIAL_NODES_TEST(FCataclysmForgeMaterialNodesFateTest,
	"Cataclysm.ForgeMaterialNodes.TemptingFateReturnsEveryMaterialAboveFiveHundredTheOptionsToo")
{
	using namespace CataclysmForgeMaterialNodesTest;
	FMaterialNodesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestNotNull(TEXT("set-up: the crafting material table"), UCataclysmDropRoll::LoadCraftingMaterialTable())
		|| !TestTrue(TEXT("set-up: the empire tree's tables"),
					 UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	Scene.Hold({{TemptingFate, 1}});
	const FName Stone3 = UCataclysmForge::StoneFor(3);
	const FName Stabilizer(UCataclysmForge::ChaosStabilizerRow);
	auto Lands = [&](int32 Kills) { return Scene.Draws(UpgradeHelm(2, Kills, 501.0f), 0.0f, 0.0f, 0.15f).bAllReturned; };
	const int32 Kills = KillsWhere(Lands);
	if (!TestTrue(TEXT("set-up: a helm at 501 residue whose draw lands"), Kills != INDEX_NONE))
	{
		return false;
	}
	auto UpgradeWithStabilizer = [&](float Residue, int32& OutStones, int32& OutStabilizers)
	{
		Scene.Bag()->RemoveMaterial(Stone3, Scene.Bag()->CountOfMaterial(Stone3));
		Scene.Bag()->RemoveMaterial(Stabilizer, Scene.Bag()->CountOfMaterial(Stabilizer));
		const int32 Cell = Scene.Bag()->AddItem(UpgradeHelm(2, Kills, Residue));
		Scene.Bag()->AddMaterial(Stone3, 2);
		Scene.Bag()->AddMaterial(Stabilizer, 1);
		FCataclysmForgeCraftOptions Options;
		Options.bChaosStabilizer = true;
		FCataclysmForgeUpgrade Done;
		FString Message;
		const ECataclysmForgeResult Result =
			UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message, Options);
		OutStones = Scene.Bag()->CountOfMaterial(Stone3);
		OutStabilizers = Scene.Bag()->CountOfMaterial(Stabilizer);
		if (Scene.Bag()->ItemAt(Cell))
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return Result == ECataclysmForgeResult::Done;
	};
	int32 Stones = -1;
	int32 Stabilizers = -1;
	int32 LowStones = -1;
	int32 LowStabilizers = -1;
	const bool bDone = UpgradeWithStabilizer(501.0f, Stones, Stabilizers);
	const bool bLowDone = UpgradeWithStabilizer(400.0f, LowStones, LowStabilizers);
	AddInfo(FString::Printf(TEXT("FATE: kills %d; at 501 done %s, %d stones and %d Stabilizer after; at 400 done %s, %d "
								 "and %d"),
							Kills, bDone ? TEXT("yes") : TEXT("NO"), Stones, Stabilizers, bLowDone ? TEXT("yes") : TEXT("NO"),
							LowStones, LowStabilizers));

	TestTrue(TEXT("the upgrade at 501 residue is done"), bDone);
	TestEqual(TEXT("**its two stones are returned**"), Stones, 2);
	TestEqual(TEXT("**and the option's Stabilizer (Q17)**"), Stabilizers, 1);
	TestTrue(TEXT("CONTROL: the upgrade at 400 residue is done"), bLowDone);
	TestTrue(TEXT("CONTROL: and returns nothing"), LowStones == 0 && LowStabilizers == 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M6. A combine keeps nothing, whatever the tree holds (Q11).
// --------------------------------------------------------------------------------------------------------------------
/**
 * In a world, with all three nodes held at full points: two Upgrade Stone +1 combined make one Upgrade Stone +2 and
 * leave none of the +1, on any day: a kept stone would make stones from nothing. Prints "COMBINE".
 */
CATACLYSM_MATERIAL_NODES_TEST(FCataclysmForgeMaterialNodesCombineTest,
	"Cataclysm.ForgeMaterialNodes.ACombineKeepsNothingWhateverTheTreeHolds")
{
	using namespace CataclysmForgeMaterialNodesTest;
	FMaterialNodesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	Scene.Hold({{MaterialConservation, 10}, {ResourceManagement, 20}, {TemptingFate, 1}});
	const FName Stone1 = UCataclysmForge::StoneFor(1);
	const FName Stone2 = UCataclysmForge::StoneFor(2);
	int32 Combined = 0;
	int32 OnesLeftAfterEach = 0;
	for (int32 Time = 0; Time < 10; ++Time)
	{
		Scene.Bag()->AddMaterial(Stone1, 2);
		FCataclysmForgeCombine Done;
		FString Reason;
		Combined += UCataclysmForgeCraft::Combine(Scene.World, 1, Done, Reason) ? 1 : 0;
		OnesLeftAfterEach += Scene.Bag()->CountOfMaterial(Stone1);
		Scene.Run->AdvanceDays(1); // ANOTHER DAY, so no draw of the day could have kept them.
	}
	AddInfo(FString::Printf(TEXT("COMBINE: %d of 10 combined; +1 stones left over all %d; +2 stones %d"), Combined,
							OnesLeftAfterEach, Scene.Bag()->CountOfMaterial(Stone2)));

	TestEqual(TEXT("each of 10 combines is done"), Combined, 10);
	TestEqual(TEXT("**and leaves none of its two +1 stones**"), OnesLeftAfterEach, 0);
	TestEqual(TEXT("making ten +2 stones"), Scene.Bag()->CountOfMaterial(Stone2), 10);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M7. The cleanse with Resource Management keeps its Purified Essence (N3).
// --------------------------------------------------------------------------------------------------------------------
/**
 * In a world, with Resource Management at 20 points (20%): a helm at 300 residue whose draw lands is cleansed, its
 * residue halved, and its Purified Essence (tier 5) is back in the bag. CONTROL: one whose draw misses is cleansed and
 * its Essence is gone. Prints "CLEANSE KEPT".
 */
CATACLYSM_MATERIAL_NODES_TEST(FCataclysmForgeMaterialNodesCleanseTest,
	"Cataclysm.ForgeMaterialNodes.TheCleanseWithResourceManagementKeepsItsEssence")
{
	using namespace CataclysmForgeMaterialNodesTest;
	FMaterialNodesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestNotNull(TEXT("set-up: the crafting material table"), UCataclysmDropRoll::LoadCraftingMaterialTable())
		|| !TestTrue(TEXT("set-up: the empire tree's tables"),
					 UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	Scene.Hold({{ResourceManagement, 20}});
	const FName Essence(UCataclysmForge::PurifiedEssenceRow);
	auto Lands = [&](int32 Kills) { return Scene.Draws(UpgradeHelm(2, Kills, 300.0f), 0.0f, 0.20f, 0.0f).bRareKept; };
	// EACH SEARCHED ON THE DAY IT IS CRAFTED: the first cleanse's day moves the day the second draws on.
	const int32 LandsKills = KillsWhere(Lands);
	if (!TestTrue(TEXT("set-up: a helm whose draw lands"), LandsKills != INDEX_NONE))
	{
		return false;
	}
	auto CleanseOnce = [&](int32 Kills, float& OutResidueAfter, int32& OutEssenceAfter)
	{
		Scene.Bag()->RemoveMaterial(Essence, Scene.Bag()->CountOfMaterial(Essence));
		const int32 Cell = Scene.Bag()->AddItem(UpgradeHelm(2, Kills, 300.0f));
		Scene.Bag()->AddMaterial(Essence, 1);
		FCataclysmForgeCleanse Done;
		FString Message;
		const ECataclysmForgeResult Result =
			UCataclysmForgeCraft::Cleanse(Scene.World, FCataclysmForgeTarget::Carried(Cell), Done, Message);
		OutResidueAfter = Scene.Bag()->ItemAt(Cell) ? Scene.Bag()->ItemAt(Cell)->Residue : -1.0f;
		OutEssenceAfter = Scene.Bag()->CountOfMaterial(Essence);
		if (Scene.Bag()->ItemAt(Cell))
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return Result == ECataclysmForgeResult::Done;
	};
	float KeptResidue = -1.0f;
	float MissedResidue = -1.0f;
	int32 KeptEssence = -1;
	int32 MissedEssence = -1;
	const bool bKeptDone = CleanseOnce(LandsKills, KeptResidue, KeptEssence);
	const int32 MissesKills = KillsWhere([&](int32 Kills) { return !Lands(Kills); });
	if (!TestTrue(TEXT("set-up: a helm whose draw misses, on the day after the first"), MissesKills != INDEX_NONE))
	{
		return false;
	}
	const bool bMissedDone = CleanseOnce(MissesKills, MissedResidue, MissedEssence);
	AddInfo(FString::Printf(TEXT("CLEANSE KEPT: landing (kills %d) done %s, residue %.1f, %d Essence after; missing "
								 "(kills %d) done %s, residue %.1f, %d Essence after"),
							LandsKills, bKeptDone ? TEXT("yes") : TEXT("NO"), KeptResidue, KeptEssence, MissesKills,
							bMissedDone ? TEXT("yes") : TEXT("NO"), MissedResidue, MissedEssence));

	TestTrue(TEXT("the cleanse whose draw lands is done"), bKeptDone);
	TestEqual(TEXT("its residue halved"), KeptResidue, 150.0f, 0.01f);
	TestEqual(TEXT("**and its Purified Essence is back in the bag (N3)**"), KeptEssence, 1);
	TestTrue(TEXT("CONTROL: the cleanse whose draw misses is done"), bMissedDone);
	TestEqual(TEXT("CONTROL: and its Essence is gone"), MissedEssence, 0);
	return true;
}

#undef CATACLYSM_MATERIAL_NODES_TEST

#endif // WITH_AUTOMATION_TESTS
