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
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

/**
 * ANCIENT TECHNIQUES AND RESIDUE PROTOCOLS. #2741's crafting group, slice C6, ruled by the coordinating session on
 * 2026-11-25 under the owner's delegation.
 *
 * Ancient Techniques takes a day a point off a forge craft on an item with four affixes or more before the craft (R2),
 * added to Master Craftsman's days off; a craft of a day keeps one. Residue Protocols ignores 5% a point of the Worn
 * Residue the Consumption Threshold counts (R1): the equip crossing, the forge's crossing and the character sheet's line
 * read `UCataclysmWearing::CountedWornResidue`; the items keep their residue.
 *
 * WRITTEN BEFORE A BUILD. The scene is `CataclysmForgeAffixNodesTests.cpp`'s, copied into this file's namespace.
 */
namespace CataclysmForgeAncientProtocolsTest
{
	/** The two nodes' ids in `docs/Empire_Development_Tree_Final.json`, and Master Craftsman's; their full points. */
	const FName AncientTechniques(TEXT("basic_arts_t4e"));		// 10 points
	const FName ResidueProtocols(TEXT("basic_0000005h"));		// 10
	const FName MasterCraftsman(TEXT("basic_00000057"));		// 10

	FCataclysmRolledAffix RolledAffix(const TCHAR* Row)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Row);
		Out.Tier = 2;
		Out.Roll = 0.5f;
		return Out;
	}

	/** A +2 helm carrying `Residue` and `AffixCount` affixes of four different stat groups. */
	FCataclysmItem Helm(int32 AffixCount, float Residue)
	{
		const TCHAR* const Rows[] = {TEXT("Stat_Flat_armor"), TEXT("Stat_Flat_evasion"), TEXT("Stat_Flat_block_chance"),
									 TEXT("Stat_Flat_damage_reduction")};
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = 2;
		Item.Residue = Residue;
		for (int32 Index = 0; Index < AffixCount && Index < 4; ++Index)
		{
			Item.Affixes.Add(RolledAffix(Rows[Index]));
		}
		return Item;
	}

	/** A cuirass carrying `Residue`. */
	FCataclysmItem Cuirass(float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Chest_Cuirass"));
		Item.Residue = Residue;
		return Item;
	}


	struct FProtocolsScene
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
	FProtocolsScene MakeScene()
	{
		FProtocolsScene Out;
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
		Out.Run->Begin(/* InSeed */ 2741, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}
}

#define CATACLYSM_ANCIENT_PROTOCOLS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// P1. The tree gives the two nodes' figures.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Read through the generated tables at full points: Ancient Techniques 10 days, Residue Protocols 0.5 of Worn Residue.
 * CONTROL: none held gives nought. Prints "TREE C6".
 */
CATACLYSM_ANCIENT_PROTOCOLS_TEST(FCataclysmForgeAncientProtocolsTreeTest,
	"Cataclysm.ForgeAncientProtocols.TheTwoNodesGiveTheirFigures")
{
	using namespace CataclysmForgeAncientProtocolsTest;
	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(AncientTechniques, 10);
	Tree.Add(ResidueProtocols, 10);
	const FCataclysmEmpireTreePlayerBonuses All = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreePlayerBonuses None =
		UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, FCataclysmPassiveAllocation());
	AddInfo(FString::Printf(TEXT("TREE C6: all held: days off on full items %.2f, Worn Residue ignored %.3f; none held %.2f "
								 "%.2f"),
							All.CraftDaysOffOnFullItems, All.WornResidueIgnored, None.CraftDaysOffOnFullItems,
							None.WornResidueIgnored));

	TestEqual(TEXT("**Ancient Techniques: a day a point, 10**"), All.CraftDaysOffOnFullItems, 10.0f, 0.0001f);
	TestEqual(TEXT("**Residue Protocols: 5% a point, 50%**"), All.WornResidueIgnored, 0.5f, 0.0001f);
	TestTrue(TEXT("CONTROL: none held changes nothing"),
			 None.CraftDaysOffOnFullItems == 0.0f && None.WornResidueIgnored == 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// P2. Ancient Techniques takes days off a craft on an item with four affixes or more, with Master Craftsman's.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Ancient Techniques at 10 held: a helm with four affixes takes 10 days off, one with three none. With Master Craftsman
 * at 10 as well, the four-affix helm takes 20, and a craft of 5 days keeps 1. Prints "ANCIENT".
 */
CATACLYSM_ANCIENT_PROTOCOLS_TEST(FCataclysmForgeAncientProtocolsAncientTest,
	"Cataclysm.ForgeAncientProtocols.AncientTechniquesTakesDaysOffACraftOnAFourAffixItem")
{
	using namespace CataclysmForgeAncientProtocolsTest;
	FProtocolsScene Scene = MakeScene();
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
	auto DaysOffFor = [&](int32 AffixCount)
	{
		const int32 Cell = Scene.Bag()->AddItem(Helm(AffixCount, 100.0f));
		const FCataclysmForgeCosts Costs =
			UCataclysmForgeCraft::CostsFor(Scene.World, FCataclysmForgeTarget::Carried(Cell), UCataclysmForge::StoneFor(3));
		Scene.Bag()->RemoveItemAt(Cell);
		return Costs;
	};
	Scene.Hold({{AncientTechniques, 10}});
	const FCataclysmForgeCosts Four = DaysOffFor(4);
	const FCataclysmForgeCosts Three = DaysOffFor(3);
	Scene.Hold({{AncientTechniques, 10}, {MasterCraftsman, 10}});
	const FCataclysmForgeCosts Both = DaysOffFor(4);
	AddInfo(FString::Printf(TEXT("ANCIENT: four affixes %.1f days off, three %.1f; with Master Craftsman %.1f, a craft of 5 "
								 "days takes %d"),
							Four.DaysOff, Three.DaysOff, Both.DaysOff, UCataclysmForge::TreeDaysFor(5, Both)));

	TestEqual(TEXT("**four affixes before the craft: 10 days off**"), Four.DaysOff, 10.0f, 0.0001f);
	TestEqual(TEXT("**three: none**"), Three.DaysOff, 0.0f, 0.0001f);
	TestEqual(TEXT("with Master Craftsman's 10: 20"), Both.DaysOff, 20.0f, 0.0001f);
	TestEqual(TEXT("and a craft of 5 days keeps 1"), UCataclysmForge::TreeDaysFor(5, Both), 1);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// P3. Worn Residue as the threshold counts it.
// --------------------------------------------------------------------------------------------------------------------
/**
 * 1,000 with half ignored is 500; with none ignored it is 1,000 exactly; a share above 1 is held at 1 and one below 0 at
 * 0. Prints "COUNTED".
 */
CATACLYSM_ANCIENT_PROTOCOLS_TEST(FCataclysmForgeAncientProtocolsCountedTest,
	"Cataclysm.ForgeAncientProtocols.WornResidueIsCountedLessTheShareIgnored")
{
	AddInfo(FString::Printf(TEXT("COUNTED: 1000 at 0.5 %.2f, at 0 %.2f, at 1.5 %.2f, at -0.2 %.2f"),
							UCataclysmWearing::CountedWornResidueOf(1000.0f, 0.5f),
							UCataclysmWearing::CountedWornResidueOf(1000.0f, 0.0f),
							UCataclysmWearing::CountedWornResidueOf(1000.0f, 1.5f),
							UCataclysmWearing::CountedWornResidueOf(1000.0f, -0.2f)));
	TestEqual(TEXT("**half ignored: 500**"), UCataclysmWearing::CountedWornResidueOf(1000.0f, 0.5f), 500.0f, 0.0001f);
	TestTrue(TEXT("CONTROL: none ignored is the sum exactly"), UCataclysmWearing::CountedWornResidueOf(1000.0f, 0.0f) == 1000.0f);
	TestEqual(TEXT("a share above 1 is held at 1"), UCataclysmWearing::CountedWornResidueOf(1000.0f, 1.5f), 0.0f, 0.0001f);
	TestEqual(TEXT("and one below 0 at 0"), UCataclysmWearing::CountedWornResidueOf(1000.0f, -0.2f), 1000.0f, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// P4. In a world: the equip crossing counts Worn Residue with Residue Protocols, and so does the sheet's line.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A cuirass worn at 1,500 and a helm carried at 600, on tier 1 (1,950): with no node, putting the helm on takes Worn
 * Residue from 1,500 to 2,100 and crosses. With Residue Protocols at 10 it counts 750 to 1,050 and does not cross; the
 * sheet's line reads "Worn Residue 750 of 1950 (tier 1), 50% ignored by Residue Protocols"; the items keep their residue.
 * Prints "CROSSING".
 */
CATACLYSM_ANCIENT_PROTOCOLS_TEST(FCataclysmForgeAncientProtocolsCrossingTest,
	"Cataclysm.ForgeAncientProtocols.TheEquipCrossingAndTheSheetCountWornResidueWithResidueProtocols")
{
	using namespace CataclysmForgeAncientProtocolsTest;
	FProtocolsScene Scene = MakeScene();
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
	UCataclysmEquipmentComponent* Equipment = Scene.Character->GetEquipment();
	Equipment->RestoreWornGear({TPair<ECataclysmGearSlot, FCataclysmItem>(ECataclysmGearSlot::Chest, Cuirass(1500.0f))});
	const int32 Cell = Scene.Bag()->AddItem(Helm(0, 600.0f));
	const FCataclysmThresholdCrossing Bare =
		UCataclysmWearing::CrossingFor(Scene.Bag(), Equipment, Cell, 1, ECataclysmGearSlot::Head);
	Scene.Hold({{ResidueProtocols, 10}});
	const FCataclysmThresholdCrossing Ignored =
		UCataclysmWearing::CrossingFor(Scene.Bag(), Equipment, Cell, 1, ECataclysmGearSlot::Head);
	const FString Line = UCataclysmCharacterSheetLayout::WornResidueLine(
		UCataclysmWearing::CountedWornResidue(Equipment, Equipment->WornResidue()), 1,
		UCataclysmWearing::IgnoredWornResidueShare(Equipment));
	AddInfo(FString::Printf(TEXT("CROSSING: bare %.0f to %.0f of %.0f (%s); with Residue Protocols %.0f to %.0f (%s); \"%s\"; "
								 "worn %.0f"),
							Bare.Now, Bare.After, Bare.Threshold, Bare.Crosses() ? TEXT("crosses") : TEXT("does not cross"),
							Ignored.Now, Ignored.After, Ignored.Crosses() ? TEXT("crosses") : TEXT("does not cross"), *Line,
							Equipment->WornResidue()));

	TestEqual(TEXT("CONTROL: no node, 1,500 to 2,100"), Bare.After, 2100.0f, 0.01f);
	TestTrue(TEXT("CONTROL: and it crosses"), Bare.Crosses());
	TestEqual(TEXT("**Residue Protocols at 10: 750 counted**"), Ignored.Now, 750.0f, 0.01f);
	TestEqual(TEXT("**to 1,050**"), Ignored.After, 1050.0f, 0.01f);
	TestFalse(TEXT("**and it does not cross**"), Ignored.Crosses());
	TestEqual(TEXT("**the sheet's line says what the threshold counts**"), Line,
			  FString(TEXT("Worn Residue 750 of 1950 (tier 1), 50% ignored by Residue Protocols")));
	TestEqual(TEXT("the items keep their residue"), Equipment->WornResidue(), 1500.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// P5. The sheet's line with nothing ignored is as before.
// --------------------------------------------------------------------------------------------------------------------
/** "Worn Residue 2150 of 2400 (tier 2)" with no share; with a quarter, the share is named. Prints "SHEET LINE". */
CATACLYSM_ANCIENT_PROTOCOLS_TEST(FCataclysmForgeAncientProtocolsSheetTest,
	"Cataclysm.ForgeAncientProtocols.TheSheetLineNamesTheShareIgnoredOnlyWhenThereIsOne")
{
	const FString None = UCataclysmCharacterSheetLayout::WornResidueLine(2150.4f, 2);
	const FString Quarter = UCataclysmCharacterSheetLayout::WornResidueLine(1612.8f, 2, 0.25f);
	AddInfo(FString::Printf(TEXT("SHEET LINE: \"%s\"; \"%s\""), *None, *Quarter));
	TestEqual(TEXT("CONTROL: nothing ignored, the line as before"), None, FString(TEXT("Worn Residue 2150 of 2400 (tier 2)")));
	TestEqual(TEXT("**a quarter ignored is named**"), Quarter,
			  FString(TEXT("Worn Residue 1613 of 2400 (tier 2), 25% ignored by Residue Protocols")));
	return true;
}

#undef CATACLYSM_ANCIENT_PROTOCOLS_TEST

#endif // WITH_AUTOMATION_TESTS
