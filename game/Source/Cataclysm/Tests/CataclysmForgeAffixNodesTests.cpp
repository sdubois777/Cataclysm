// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
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
#include "Player/CataclysmPlayLoopLog.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

/**
 * THE EMPIRE TREE'S AFFIX NODES ON A FORGE CRAFT. #2741's crafting group, slices C3 and C4, ruled by the coordinating
 * session on 2026-11-25 (the C3/C4 detail as written, D1 to D4).
 *
 * Master's Workshop draws a roll the forge draws (a reroll's, a Deterministic Affix's, Rough Draft's affix's) from 0.02 a
 * point up to 1, and Precision Crafting's own draw (3% a point) from 0.75 up, the higher of the two. Once a craft has
 * changed the item, in this order, each on a draw of its own from the item as it stood and the day: Rough Draft (5% a
 * point) adds one affix on a free slot whose side has room, at the drop's best tier for the run; Apprentice Smith (1% a
 * point) raises one affix below tier 7 a tier, its roll kept; Quality Materials (5% a point) makes one affix's roll its
 * best. Never on a dismantle (`tools/tests/test_forge_affix_nodes.py` checks that one in the source).
 *
 * WRITTEN BEFORE A BUILD. The scene is `CataclysmForgeTreeCostsTests.cpp`'s, copied into this file's namespace.
 */
namespace CataclysmForgeAffixNodesTest
{
	/** The five nodes' ids in `docs/Empire_Development_Tree_Final.json`, and their full points. */
	const FName PrecisionCrafting(TEXT("basic_arts_t3f"));	// 15 points
	const FName MastersWorkshop(TEXT("basic_arts_t4f"));		// 15
	const FName QualityMaterials(TEXT("basic_arts_t2d"));		// 10
	const FName ApprenticeSmith(TEXT("basic_0000004y"));		// 10
	const FName RoughDraft(TEXT("basic_arts_t1d"));			// 8

	/** Rows of `game/Data/Affixes.csv` a helm can carry: two prefixes and two suffixes, each of its own stat group. */
	const TCHAR* const FlatArmor = TEXT("Stat_Flat_armor");				// prefix
	const TCHAR* const FlatEvasion = TEXT("Stat_Flat_evasion");			// prefix
	const TCHAR* const FlatBlock = TEXT("Stat_Flat_block_chance");		// suffix
	const TCHAR* const DamageReduction = TEXT("Stat_Flat_damage_reduction");	// suffix

	/** The run the pure tests draw on: a mask and a day, as a run gives them. */
	constexpr int32 TestMask = 2741;
	constexpr int32 TestDay = 5;

	FCataclysmRolledAffix Affix(const TCHAR* Row, int32 Tier, float Roll)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Row);
		Out.Tier = Tier;
		Out.Roll = Roll;
		return Out;
	}

	/** A +2 helm carrying 100 residue, these affixes and enchantments, with the kill count that picks its draws. */
	FCataclysmItem Helm(int32 Kills, TArray<FCataclysmRolledAffix> Affixes, int32 Enchantments = 0)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = 2;
		Item.Residue = 100.0f;
		Item.Kills = Kills;
		Item.Affixes = MoveTemp(Affixes);
		Item.EnchantmentCount = Enchantments;
		return Item;
	}

	/** An affix row's position, "prefix" or "suffix"; empty for a row the table lacks. */
	FString PositionOf(const UDataTable* AffixTable, FName Row)
	{
		const FCataclysmAffixRow* Found = AffixTable->FindRow<FCataclysmAffixRow>(Row, TEXT("PositionOf"), false);
		return Found != nullptr ? Found->Position : FString();
	}

	/** The piece's rarity as a number, Everyday 0 up; -1 when it has none. */
	int32 RarityNumberOf(const FCataclysmItem& Item)
	{
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		return UCataclysmItemModifiers::RarityOfItem(Item, Rarity) ? static_cast<int32>(Rarity) : -1;
	}

	struct FNodesScene
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
	FNodesScene MakeScene()
	{
		FNodesScene Out;
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

#define CATACLYSM_AFFIX_NODES_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// A1. The tree gives the five nodes' figures.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Read through the generated tables, each node at its full points: Precision Crafting 0.45, Master's Workshop 0.30,
 * Quality Materials 0.50, Apprentice Smith 0.10, Rough Draft 0.40. CONTROL: none held gives nought. Prints "TREE AFFIX".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesTreeTest, "Cataclysm.ForgeAffixNodes.TheFiveNodesGiveTheirFigures")
{
	using namespace CataclysmForgeAffixNodesTest;
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
	const FCataclysmEmpireTreePlayerBonuses All = With({{PrecisionCrafting, 15}, {MastersWorkshop, 15},
		{QualityMaterials, 10}, {ApprenticeSmith, 10}, {RoughDraft, 8}});
	AddInfo(FString::Printf(TEXT("TREE AFFIX: all held: top quarter %.3f, floor %.3f, maxed %.3f, tier raised %.3f, added "
								 "%.3f; none held %.2f %.2f %.2f %.2f %.2f"),
							All.AffixRollTopQuarterChance, All.AffixRollFloor, All.AffixRollMaxedChance,
							All.AffixTierRaisedChance, All.AffixAddedChance, None.AffixRollTopQuarterChance,
							None.AffixRollFloor, None.AffixRollMaxedChance, None.AffixTierRaisedChance,
							None.AffixAddedChance));

	TestEqual(TEXT("**Precision Crafting: 3% a point, 45%**"), All.AffixRollTopQuarterChance, 0.45f, 0.0001f);
	TestEqual(TEXT("**Master's Workshop: 0.02 a point, 0.30**"), All.AffixRollFloor, 0.30f, 0.0001f);
	TestEqual(TEXT("**Quality Materials: 5% a point, 50% (the ruling of 2026-11-25)**"), All.AffixRollMaxedChance, 0.5f,
			  0.0001f);
	TestEqual(TEXT("**Apprentice Smith: 1% a point, 10%**"), All.AffixTierRaisedChance, 0.10f, 0.0001f);
	TestEqual(TEXT("**Rough Draft: 5% a point, 40%**"), All.AffixAddedChance, 0.40f, 0.0001f);
	TestTrue(TEXT("CONTROL: none held changes nothing"),
			 None.AffixRollTopQuarterChance == 0.0f && None.AffixRollFloor == 0.0f && None.AffixRollMaxedChance == 0.0f
				 && None.AffixTierRaisedChance == 0.0f && None.AffixAddedChance == 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A2. A roll: from the floor up, and from 0.75 up on Precision Crafting's draw; the default is the draw itself.
// --------------------------------------------------------------------------------------------------------------------
/**
 * The default rolls return each draw exactly. A floor of 0.3 places a draw of 0.5 at 0.65; a top quarter that always
 * lands places it at 0.875, with or without the floor (the higher of the two). At 45%, the draw lands on some seeds and
 * not others, and on about 45 of 100 (40 to 50 over 2,000 seeds). A reroll's roll is the bare draw with no node, and the
 * floor's placing of it with Master's Workshop. Prints "ROLLS".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesRollsTest,
	"Cataclysm.ForgeAffixNodes.ARollIsDrawnFromTheFloorUpAndOnPrecisionCraftingsDrawFromThreeQuartersUp")
{
	using namespace CataclysmForgeAffixNodesTest;
	FCataclysmForgeRolls Floor;
	Floor.Floor = 0.3f;
	FCataclysmForgeRolls Always;
	Always.TopQuarterChance = 1.0f;
	FCataclysmForgeRolls Both = Floor;
	Both.TopQuarterChance = 1.0f;
	FCataclysmForgeRolls Precision = Floor;
	Precision.TopQuarterChance = 0.45f;

	bool bDefaultExact = true;
	for (const float Draw : {0.0f, 0.123f, 0.5f, 0.999f})
	{
		bDefaultExact &= FCataclysmForgeRolls().Shape(Draw, 7) == Draw;
	}
	int32 Landed = 0;
	int32 SeedLands = INDEX_NONE;
	int32 SeedMisses = INDEX_NONE;
	for (int32 Seed = 1; Seed <= 2000; ++Seed)
	{
		const bool bLands = Precision.TopQuarterLands(Seed);
		Landed += bLands ? 1 : 0;
		SeedLands = bLands && SeedLands == INDEX_NONE ? Seed : SeedLands;
		SeedMisses = !bLands && SeedMisses == INDEX_NONE ? Seed : SeedMisses;
	}
	const FCataclysmItem Item = Helm(3, {Affix(FlatArmor, 3, 0.4f)});
	const float Bare = UCataclysmForge::RerolledAffixRoll(Item, 0, TestMask, TestDay);
	const float Floored = UCataclysmForge::RerolledAffixRoll(Item, 0, TestMask, TestDay, Floor);
	const FRandomStream Stream(UCataclysmForge::RerolledAffixSeed(Item, 0, TestMask, TestDay));
	const float Drawn = Stream.FRand();
	AddInfo(FString::Printf(TEXT("ROLLS: 0.5 floored %.4f, top %.4f, both %.4f; 45%% landed on %d of 2000 (first %d, first "
								 "miss %d); reroll bare %.4f (drawn %.4f), floored %.4f"),
							Floor.Shape(0.5f, 7), Always.Shape(0.5f, 7), Both.Shape(0.5f, 7), Landed, SeedLands,
							SeedMisses, Bare, Drawn, Floored));

	TestTrue(TEXT("CONTROL: the default rolls return each draw exactly"), bDefaultExact);
	TestEqual(TEXT("**Master's Workshop: 0.5 drawn from 0.3 up is 0.65**"), Floor.Shape(0.5f, 7), 0.65f, 0.0001f);
	TestEqual(TEXT("and a draw of 0 is the floor"), Floor.Shape(0.0f, 7), 0.3f, 0.0001f);
	TestEqual(TEXT("**Precision Crafting landed: 0.5 drawn from 0.75 up is 0.875**"), Always.Shape(0.5f, 7), 0.875f, 0.0001f);
	TestEqual(TEXT("the higher of the two floors"), Both.Shape(0.5f, 7), 0.875f, 0.0001f);
	if (TestTrue(TEXT("at 45%, a seed it lands on and one it misses"), SeedLands != INDEX_NONE && SeedMisses != INDEX_NONE))
	{
		TestEqual(TEXT("landed: from 0.75 up"), Precision.Shape(0.5f, SeedLands), 0.875f, 0.0001f);
		TestEqual(TEXT("missed: from the floor up"), Precision.Shape(0.5f, SeedMisses), 0.65f, 0.0001f);
	}
	TestTrue(TEXT("its own draw lands on about 45 in 100"), Landed >= 800 && Landed <= 1000);
	TestEqual(TEXT("CONTROL: a reroll with no node draws as before this slice"), Bare, Drawn);
	TestEqual(TEXT("**a reroll with Master's Workshop: the same draw from 0.3 up**"), Floored, 0.3f + 0.7f * Drawn, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A3. The Deterministic Affix draws its roll with the rolls, and nothing else of its draw moves.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Flat armor added at tier 3 on one seed: with Master's Workshop's 0.3 its roll is the bare roll placed from 0.3 up, and
 * its row, tier and damage types are the bare plan's. CONTROL: the bare plan is possible. Prints "DETERMINISTIC".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesDeterministicTest,
	"Cataclysm.ForgeAffixNodes.TheDeterministicAffixDrawsItsRollWithTheRolls")
{
	using namespace CataclysmForgeAffixNodesTest;
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestTrue(TEXT("set-up: the affix and base tables"), AffixTable && BaseTable))
	{
		return false;
	}
	FCataclysmForgeRolls Floor;
	Floor.Floor = 0.3f;
	const FCataclysmItem Item = Helm(3, {Affix(FlatBlock, 2, 0.5f)});
	const FCataclysmForgeAffixChange Bare = UCataclysmForge::DeterministicAffixOf(
		Item, FName(FlatArmor), 3, 12345, AffixTable, BaseTable, 1, 1, 1000000);
	const FCataclysmForgeAffixChange Floored = UCataclysmForge::DeterministicAffixOf(
		Item, FName(FlatArmor), 3, 12345, AffixTable, BaseTable, 1, 1, 1000000, FCataclysmForgePricing(), Floor);
	AddInfo(FString::Printf(TEXT("DETERMINISTIC: bare %s roll %.4f; floored %s roll %.4f"),
							Bare.bPossible ? TEXT("possible") : *Bare.Refusal, Bare.Roll,
							Floored.bPossible ? TEXT("possible") : *Floored.Refusal, Floored.Roll));

	TestTrue(TEXT("CONTROL: the bare plan is possible"), Bare.bPossible);
	TestEqual(TEXT("**its roll is the bare roll from 0.3 up**"), Floored.Roll, 0.3f + 0.7f * Bare.Roll, 0.0001f);
	TestTrue(TEXT("and the rest of its draw is the bare plan's"),
			 Floored.ToRow == Bare.ToRow && Floored.Tier == Bare.Tier && Floored.ToDamageTypes == Bare.ToDamageTypes);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A4. Rough Draft adds one affix on a free slot whose side has room, at the drop's best tier, and promotes the piece.
// --------------------------------------------------------------------------------------------------------------------
/**
 * At 100%: a helm with Flat armor gains a second affix at tier 2 (the best a tier 1 run's drop carries) of another stat
 * group, last, its roll from 0 up to 1, and is one rarity higher. A helm with two prefixes and a suffix gains a suffix.
 * CONTROL: a helm with four affixes, one with three and an enchantment, and a 0% chance gain none. Prints "ROUGH DRAFT".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesRoughDraftTest,
	"Cataclysm.ForgeAffixNodes.RoughDraftAddsOneAffixOnAFreeSlotAtTheDropsBestTier")
{
	using namespace CataclysmForgeAffixNodesTest;
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestTrue(TEXT("set-up: the affix and base tables"), AffixTable && BaseTable))
	{
		return false;
	}
	FCataclysmForgeAffixNodes Nodes;
	Nodes.AddedChance = 1.0f;
	auto Apply = [&](const FCataclysmItem& Before, const FCataclysmForgeAffixNodes& With, FCataclysmItem& OutChanged)
	{
		OutChanged = Before;
		return UCataclysmForge::AffixNodesOn(OutChanged, Before, With, TestMask, TestDay, AffixTable, BaseTable, 1);
	};
	const FCataclysmItem One = Helm(3, {Affix(FlatArmor, 3, 0.4f)});
	FCataclysmItem OneAfter;
	const FCataclysmForgeAffixNodesDone OneDone = Apply(One, Nodes, OneAfter);
	const FCataclysmItem Three = Helm(3, {Affix(FlatArmor, 3, 0.4f), Affix(FlatEvasion, 3, 0.4f), Affix(FlatBlock, 3, 0.4f)});
	FCataclysmItem ThreeAfter;
	const FCataclysmForgeAffixNodesDone ThreeDone = Apply(Three, Nodes, ThreeAfter);
	const FCataclysmItem Full = Helm(3, {Affix(FlatArmor, 3, 0.4f), Affix(FlatEvasion, 3, 0.4f), Affix(FlatBlock, 3, 0.4f),
										  Affix(DamageReduction, 3, 0.4f)});
	FCataclysmItem FullAfter;
	const FCataclysmForgeAffixNodesDone FullDone = Apply(Full, Nodes, FullAfter);
	const FCataclysmItem Enchanted = Helm(3, {Affix(FlatArmor, 3, 0.4f), Affix(FlatEvasion, 3, 0.4f),
											   Affix(FlatBlock, 3, 0.4f)}, 1);
	FCataclysmItem EnchantedAfter;
	const FCataclysmForgeAffixNodesDone EnchantedDone = Apply(Enchanted, Nodes, EnchantedAfter);
	FCataclysmItem NoneAfter;
	const FCataclysmForgeAffixNodesDone NoneDone = Apply(One, FCataclysmForgeAffixNodes(), NoneAfter);
	const FCataclysmRolledAffix* Added = OneAfter.Affixes.IsValidIndex(1) ? &OneAfter.Affixes[1] : nullptr;
	TSet<FString> ArmorGroups;
	UCataclysmForge::HeldGroupsOf(One, AffixTable, ArmorGroups);
	TSet<FString> AddedGroups;
	if (Added != nullptr)
	{
		UCataclysmForge::HeldGroupsOf(Helm(3, {*Added}), AffixTable, AddedGroups);
	}
	AddInfo(FString::Printf(TEXT("ROUGH DRAFT: one affix: added at %d, %s tier %d roll %.4f, rarity %d to %d; two prefixes "
								 "and a suffix: added %s (%s); full %d, enchanted %d, none %d"),
							OneDone.Added, Added ? *Added->Affix.ToString() : TEXT("-"), Added ? Added->Tier : 0,
							Added ? Added->Roll : -1.0f, RarityNumberOf(One), RarityNumberOf(OneAfter),
							*ThreeDone.AddedRow.ToString(), *PositionOf(AffixTable, ThreeDone.AddedRow), FullDone.Added,
							EnchantedDone.Added, NoneDone.Added));

	if (!TestNotNull(TEXT("**at 100% the helm gains a second affix**"), Added))
	{
		return false;
	}
	TestEqual(TEXT("last"), OneDone.Added, 1);
	TestEqual(TEXT("**at the best tier a tier 1 run's drop carries**"), Added->Tier,
			  UCataclysmDropRoll::MaxAffixTierOnADrop(1));
	TestEqual(TEXT("which is tier 2"), Added->Tier, 2);
	TestTrue(TEXT("of no stat group the helm held"), AddedGroups.Num() > 0 && AddedGroups.Intersect(ArmorGroups).Num() == 0);
	TestTrue(TEXT("its roll from 0 up to 1"), Added->Roll >= 0.0f && Added->Roll < 1.0f);
	TestTrue(TEXT("its row and tier are said"), OneDone.AddedRow == Added->Affix && OneDone.AddedTier == Added->Tier);
	TestEqual(TEXT("**the piece is one rarity higher**"), RarityNumberOf(OneAfter), RarityNumberOf(One) + 1);
	TestEqual(TEXT("**two prefixes and a suffix: a suffix is added**"), PositionOf(AffixTable, ThreeDone.AddedRow),
			  FString(TEXT("suffix")));
	TestEqual(TEXT("and it is the fourth"), ThreeAfter.Affixes.Num(), 4);
	TestTrue(TEXT("CONTROL: four affixes, nothing added"), FullDone.Added == INDEX_NONE && FullAfter.Affixes.Num() == 4);
	TestTrue(TEXT("CONTROL: three affixes and an enchantment, nothing added"),
			 EnchantedDone.Added == INDEX_NONE && EnchantedAfter.Affixes.Num() == 3);
	TestTrue(TEXT("CONTROL: no node, nothing added"), !NoneDone.Any() && NoneAfter.Affixes.Num() == 1);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A5. Apprentice Smith raises one affix below tier 7 a tier and keeps its roll; Quality Materials makes one roll its best.
// --------------------------------------------------------------------------------------------------------------------
/**
 * At 100%: on a helm with Flat armor at tier 7 and Flat block chance at tier 3 (0.42), Apprentice Smith raises the block
 * chance to tier 4 at 0.42; on a helm with Flat armor at its best and Flat block chance at 0.37, Quality Materials makes
 * the block chance 1 at its tier. CONTROL: every affix at tier 7, or every roll at its best, and nothing changes. Prints
 * "SMITH AND MATERIALS".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesSmithTest,
	"Cataclysm.ForgeAffixNodes.ApprenticeSmithRaisesATierAndQualityMaterialsMakesARollItsBest")
{
	using namespace CataclysmForgeAffixNodesTest;
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestTrue(TEXT("set-up: the affix and base tables"), AffixTable && BaseTable))
	{
		return false;
	}
	FCataclysmForgeAffixNodes Smith;
	Smith.TierRaisedChance = 1.0f;
	FCataclysmForgeAffixNodes Materials;
	Materials.RollMaxedChance = 1.0f;
	auto Apply = [&](const FCataclysmItem& Before, const FCataclysmForgeAffixNodes& With, FCataclysmItem& OutChanged)
	{
		OutChanged = Before;
		return UCataclysmForge::AffixNodesOn(OutChanged, Before, With, TestMask, TestDay, AffixTable, BaseTable, 1);
	};
	FCataclysmItem Raised;
	const FCataclysmForgeAffixNodesDone RaisedDone =
		Apply(Helm(3, {Affix(FlatArmor, 7, 0.5f), Affix(FlatBlock, 3, 0.42f)}), Smith, Raised);
	FCataclysmItem AllSeven;
	const FCataclysmForgeAffixNodesDone AllSevenDone =
		Apply(Helm(3, {Affix(FlatArmor, 7, 0.5f), Affix(FlatBlock, 7, 0.42f)}), Smith, AllSeven);
	FCataclysmItem Maxed;
	const FCataclysmForgeAffixNodesDone MaxedDone =
		Apply(Helm(3, {Affix(FlatArmor, 3, 1.0f), Affix(FlatBlock, 4, 0.37f)}), Materials, Maxed);
	FCataclysmItem AllBest;
	const FCataclysmForgeAffixNodesDone AllBestDone =
		Apply(Helm(3, {Affix(FlatArmor, 3, 1.0f), Affix(FlatBlock, 4, 1.0f)}), Materials, AllBest);
	AddInfo(FString::Printf(TEXT("SMITH AND MATERIALS: raised affix %d from tier %d to %d at %.2f; all at 7: %d; made best "
								 "affix %d from %.2f to %.2f at tier %d; all at best: %d"),
							RaisedDone.Raised, RaisedDone.RaisedFromTier, Raised.Affixes[1].Tier, Raised.Affixes[1].Roll,
							AllSevenDone.Raised, MaxedDone.Maxed, MaxedDone.MaxedFromRoll, Maxed.Affixes[1].Roll,
							Maxed.Affixes[1].Tier, AllBestDone.Maxed));

	TestEqual(TEXT("**Apprentice Smith: the affix below tier 7**"), RaisedDone.Raised, 1);
	TestEqual(TEXT("rises a tier"), Raised.Affixes[1].Tier, 4);
	TestEqual(TEXT("its roll kept"), Raised.Affixes[1].Roll, 0.42f);
	TestTrue(TEXT("and the other is untouched"), Raised.Affixes[0].Tier == 7 && Raised.Affixes[0].Roll == 0.5f);
	TestTrue(TEXT("its row and tier before are said"), RaisedDone.RaisedRow == FName(FlatBlock) && RaisedDone.RaisedFromTier == 3);
	TestTrue(TEXT("CONTROL: every affix at tier 7, nothing raised"),
			 AllSevenDone.Raised == INDEX_NONE && AllSeven.Affixes[0].Tier == 7 && AllSeven.Affixes[1].Tier == 7);
	TestEqual(TEXT("**Quality Materials: the affix below its best**"), MaxedDone.Maxed, 1);
	TestEqual(TEXT("its roll made its best"), Maxed.Affixes[1].Roll, 1.0f);
	TestEqual(TEXT("at its tier"), Maxed.Affixes[1].Tier, 4);
	TestEqual(TEXT("the roll it had is said"), MaxedDone.MaxedFromRoll, 0.37f);
	TestTrue(TEXT("CONTROL: every roll at its best, nothing made best"), AllBestDone.Maxed == INDEX_NONE && !AllBestDone.Any());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A6. The three act in order, each on the affixes as they stand, and the same item on the same day draws the same.
// --------------------------------------------------------------------------------------------------------------------
/**
 * All three at 100% on a helm whose one affix is at tier 7 and its best: Rough Draft adds an affix at tier 2, Apprentice
 * Smith can only raise that one (to tier 3), and Quality Materials can only make that one's roll its best (D1). The words
 * say the three in that order. At chances below 100% two calls on the same item and day draw the same. Prints "ORDER".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesOrderTest,
	"Cataclysm.ForgeAffixNodes.TheThreeActInOrderEachOnTheAffixesAsTheyStand")
{
	using namespace CataclysmForgeAffixNodesTest;
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestTrue(TEXT("set-up: the affix and base tables"), AffixTable && BaseTable))
	{
		return false;
	}
	FCataclysmForgeAffixNodes All;
	All.AddedChance = 1.0f;
	All.TierRaisedChance = 1.0f;
	All.RollMaxedChance = 1.0f;
	const FCataclysmItem Before = Helm(3, {Affix(FlatArmor, 7, 1.0f)});
	FCataclysmItem Changed = Before;
	const FCataclysmForgeAffixNodesDone Done =
		UCataclysmForge::AffixNodesOn(Changed, Before, All, TestMask, TestDay, AffixTable, BaseTable, 1);
	const FString Words = UCataclysmForge::AffixNodesDoneText(Done, AffixTable);

	FCataclysmForgeAffixNodes Some;
	Some.AddedChance = 0.4f;
	Some.TierRaisedChance = 0.5f;
	Some.RollMaxedChance = 0.5f;
	const FCataclysmItem Mixed = Helm(9, {Affix(FlatArmor, 3, 0.4f), Affix(FlatBlock, 5, 0.6f)});
	FCataclysmItem First = Mixed;
	FCataclysmItem Second = Mixed;
	const FCataclysmForgeAffixNodesDone FirstDone =
		UCataclysmForge::AffixNodesOn(First, Mixed, Some, TestMask, TestDay, AffixTable, BaseTable, 1);
	const FCataclysmForgeAffixNodesDone SecondDone =
		UCataclysmForge::AffixNodesOn(Second, Mixed, Some, TestMask, TestDay, AffixTable, BaseTable, 1);
	bool bSame = First.Affixes.Num() == Second.Affixes.Num() && FirstDone.Added == SecondDone.Added
		&& FirstDone.Raised == SecondDone.Raised && FirstDone.Maxed == SecondDone.Maxed;
	for (int32 Index = 0; bSame && Index < First.Affixes.Num(); ++Index)
	{
		bSame = First.Affixes[Index].Affix == Second.Affixes[Index].Affix && First.Affixes[Index].Tier == Second.Affixes[Index].Tier
			&& First.Affixes[Index].Roll == Second.Affixes[Index].Roll;
	}
	AddInfo(FString::Printf(TEXT("ORDER: added %d, raised %d from %d, made best %d from %.4f: \"%s\"; at below 100%%, twice "
								 "the same: %s (added %d, raised %d, made best %d)"),
							Done.Added, Done.Raised, Done.RaisedFromTier, Done.Maxed, Done.MaxedFromRoll, *Words,
							bSame ? TEXT("yes") : TEXT("no"), FirstDone.Added, FirstDone.Raised, FirstDone.Maxed));

	TestEqual(TEXT("**Rough Draft first: an affix added**"), Done.Added, 1);
	TestEqual(TEXT("**then Apprentice Smith raises the one it added, the only one below tier 7**"), Done.Raised, 1);
	TestEqual(TEXT("from the tier it was added at"), Done.RaisedFromTier, 2);
	TestEqual(TEXT("to tier 3"), Changed.Affixes.IsValidIndex(1) ? Changed.Affixes[1].Tier : 0, 3);
	TestEqual(TEXT("**then Quality Materials makes its roll its best, the only one below**"), Done.Maxed, 1);
	TestEqual(TEXT("to 1"), Changed.Affixes.IsValidIndex(1) ? Changed.Affixes[1].Roll : 0.0f, 1.0f);
	TestTrue(TEXT("the first affix is untouched"), Changed.Affixes[0].Tier == 7 && Changed.Affixes[0].Roll == 1.0f);
	const FString Name = UCataclysmForge::AffixNameOf(Done.AddedRow, AffixTable);
	TestEqual(TEXT("the words say the three in order"), Words,
			  FString::Printf(TEXT("added %s at tier 2; raised %s from tier 2 to tier 3; made %s's roll its best, %d to 100 "
								   "percent"),
							  *Name, *Name, *Name, UCataclysmForge::RollPercent(Done.MaxedFromRoll)));
	TestTrue(TEXT("**the same item on the same day draws the same**"), bSame);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A7. In a world: an upgrade takes Quality Materials, and a reroll Master's Workshop's floor, as the question says.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Quality Materials at 10 (50%): a +2 helm whose one affix is at 0.4 and whose draw lands is upgraded to +3 and its
 * affix's roll is 1, and one whose draw misses keeps 0.4; the question's words end " The empire tree's affix nodes: a 50%
 * chance one affix's roll is made its best." Master's
 * Workshop alone at 15: a reroll of that affix ends at the bare draw placed from 0.3 up, and the reroll's words begin
 * " Its roll is drawn from 30 percent up." CONTROL: with no node held the upgrade leaves the roll at 0.4, the reroll
 * draws the bare roll, and the words are empty. Prints "IN A WORLD".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesWorldTest,
	"Cataclysm.ForgeAffixNodes.AnUpgradeTakesQualityMaterialsAndARerollTheFloorAsTheQuestionSays")
{
	using namespace CataclysmForgeAffixNodesTest;
	FNodesScene Scene = MakeScene();
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
	// A HELM WHOSE QUALITY MATERIALS DRAW TODAY LANDS (or misses) at 50%, read from the helm in the bag, as the craft reads it.
	auto MaterialsLand = [&](const FCataclysmItem& Item)
	{
		FRandomStream Stream(UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::QualityMaterialsSalt, 0,
															 Scene.Run->WalkSeedMask, Scene.Run->Day()));
		return Stream.GetFraction() < 0.5f;
	};
	auto PlaceHelm = [&](TFunctionRef<bool(const FCataclysmItem&)> Wanted)
	{
		for (int32 Kills = 0; Kills < 2000; ++Kills)
		{
			const int32 Cell = Scene.Bag()->AddItem(Helm(Kills, {Affix(FlatArmor, 3, 0.4f)}));
			if (Scene.Bag()->ItemAt(Cell) != nullptr && Wanted(*Scene.Bag()->ItemAt(Cell)))
			{
				return Cell;
			}
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return static_cast<int32>(INDEX_NONE);
	};
	auto UpgradeOnce = [&](TFunctionRef<bool(const FCataclysmItem&)> Wanted, float& OutRoll, FString& OutWords)
	{
		const int32 Cell = PlaceHelm(Wanted);
		if (Cell == INDEX_NONE)
		{
			OutRoll = -1.0f;
			return false;
		}
		Scene.Bag()->AddMaterial(Stone3, 2);
		const FCataclysmForgeTarget Target = FCataclysmForgeTarget::Carried(Cell);
		OutWords = UCataclysmForgeCraft::AffixNodesTextFor(Scene.World, /*bDrawsARoll=*/false);
		FCataclysmForgeUpgrade Done;
		FString Message;
		const bool bDone = UCataclysmForgeCraft::Upgrade(Scene.World, Target, false, Done, Message) == ECataclysmForgeResult::Done;
		const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
		OutRoll = After != nullptr && After->Affixes.Num() == 1 ? After->Affixes[0].Roll : -1.0f;
		if (After != nullptr)
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return bDone;
	};
	auto RerollOnce = [&](float& OutRoll, float& OutBare, FString& OutWords)
	{
		const FCataclysmItem Placed = Helm(12, {Affix(FlatArmor, 3, 0.4f)});
		const int32 Cell = Scene.Bag()->AddItem(Placed);
		Scene.Bag()->AddMaterial(FName(UCataclysmForge::CorruptedMoteRow), 1);
		const FCataclysmForgeTarget Target = FCataclysmForgeTarget::Carried(Cell);
		OutWords = UCataclysmForgeCraft::AffixNodesTextFor(Scene.World, /*bDrawsARoll=*/true);
		const FCataclysmItem* Held = Scene.Bag()->ItemAt(Cell);
		OutBare = Held != nullptr ? UCataclysmForge::RerolledAffixRoll(*Held, 0, Scene.Run->WalkSeedMask, Scene.Run->Day())
								  : -1.0f;
		FCataclysmForgeAffixWork Done;
		FString Message;
		const bool bDone = UCataclysmForgeCraft::CraftAffix(Scene.World, Target, ECataclysmForgeAffixCraft::RerollValue, 0,
															false, Done, Message) == ECataclysmForgeResult::Done;
		const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
		OutRoll = After != nullptr && After->Affixes.Num() == 1 ? After->Affixes[0].Roll : -1.0f;
		if (After != nullptr)
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return bDone;
	};
	float BareUpgradeRoll = 0.0f;
	FString BareUpgradeWords;
	const auto Any = [](const FCataclysmItem&) { return true; };
	const bool bBareUpgrade = UpgradeOnce(Any, BareUpgradeRoll, BareUpgradeWords);
	float BareRerollRoll = 0.0f;
	float BareRerollDraw = 0.0f;
	FString BareRerollWords;
	const bool bBareReroll = RerollOnce(BareRerollRoll, BareRerollDraw, BareRerollWords);
	Scene.Hold({{QualityMaterials, 10}});
	float MaterialsRoll = 0.0f;
	FString MaterialsWords;
	const bool bMaterials = UpgradeOnce(MaterialsLand, MaterialsRoll, MaterialsWords);
	float MissedRoll = 0.0f;
	FString MissedWords;
	const bool bMissed = UpgradeOnce([&](const FCataclysmItem& Item) { return !MaterialsLand(Item); }, MissedRoll, MissedWords);
	Scene.Hold({{MastersWorkshop, 15}});
	float WorkshopRoll = 0.0f;
	float WorkshopDraw = 0.0f;
	FString WorkshopWords;
	const bool bWorkshop = RerollOnce(WorkshopRoll, WorkshopDraw, WorkshopWords);
	AddInfo(FString::Printf(TEXT("IN A WORLD: bare upgrade %.4f \"%s\", bare reroll %.4f of %.4f \"%s\"; Quality Materials "
								 "upgrade %.4f \"%s\", missed %.4f; Master's Workshop reroll %.4f of %.4f \"%s\""),
							BareUpgradeRoll, *BareUpgradeWords, BareRerollRoll, BareRerollDraw, *BareRerollWords,
							MaterialsRoll, *MaterialsWords, MissedRoll, WorkshopRoll, WorkshopDraw, *WorkshopWords));

	TestTrue(TEXT("set-up: the five crafts are done"), bBareUpgrade && bBareReroll && bMaterials && bMissed && bWorkshop);
	TestEqual(TEXT("CONTROL: no node, the upgrade leaves the roll"), BareUpgradeRoll, 0.4f);
	TestEqual(TEXT("CONTROL: no node, the reroll draws the bare roll"), BareRerollRoll, BareRerollDraw);
	TestTrue(TEXT("CONTROL: no node, no words"), BareUpgradeWords.IsEmpty() && BareRerollWords.IsEmpty());
	TestEqual(TEXT("**Quality Materials, its draw landed: the upgraded helm's roll is its best**"), MaterialsRoll, 1.0f);
	TestEqual(TEXT("**its draw missed: the roll is kept**"), MissedRoll, 0.4f);
	TestEqual(TEXT("and the question said so"), MaterialsWords,
			  FString(TEXT(" The empire tree's affix nodes: a 50% chance one affix's roll is made its best.")));
	TestEqual(TEXT("**Master's Workshop: the reroll's draw from 0.3 up**"), WorkshopRoll, 0.3f + 0.7f * WorkshopDraw, 0.0001f);
	TestEqual(TEXT("and the question said so"), WorkshopWords, FString(TEXT(" Its roll is drawn from 30 percent up.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A8. The words: the chances, the rolls, what was done, and the play loop line.
// --------------------------------------------------------------------------------------------------------------------
/**
 * The three chances at full points in their order; only Rough Draft; none. The rolls with both nodes, the floor alone,
 * Precision Crafting alone, neither. What was done, and the line that says it. Prints "AFFIX WORDS".
 */
CATACLYSM_AFFIX_NODES_TEST(FCataclysmForgeAffixNodesWordsTest,
	"Cataclysm.ForgeAffixNodes.TheWordsSayTheChancesTheRollsAndWhatWasDone")
{
	using namespace CataclysmForgeAffixNodesTest;
	FCataclysmForgeAffixNodes All;
	All.AddedChance = 0.4f;
	All.TierRaisedChance = 0.1f;
	All.RollMaxedChance = 1.0f;
	FCataclysmForgeAffixNodes Draft;
	Draft.AddedChance = 0.4f;
	FCataclysmForgeRolls Both;
	Both.Floor = 0.3f;
	Both.TopQuarterChance = 0.45f;
	FCataclysmForgeRolls FloorOnly;
	FloorOnly.Floor = 0.3f;
	FCataclysmForgeRolls TopOnly;
	TopOnly.TopQuarterChance = 0.45f;
	FCataclysmForgeAffixNodesDone Done;
	Done.Added = 1;
	Done.AddedRow = FName(FlatArmor);
	Done.AddedTier = 2;
	Done.Maxed = 0;
	Done.MaxedRow = FName(FlatBlock);
	Done.MaxedFromRoll = 0.37f;
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const FString DoneWords = UCataclysmForge::AffixNodesDoneText(Done, AffixTable);
	AddInfo(FString::Printf(TEXT("AFFIX WORDS: \"%s\"; \"%s\"; \"%s\"; \"%s\"; \"%s\"; \"%s\""),
							*UCataclysmForge::AffixNodesChanceText(All), *UCataclysmForge::AffixNodesChanceText(Draft),
							*UCataclysmForge::RollsText(Both), *UCataclysmForge::RollsText(FloorOnly),
							*UCataclysmForge::RollsText(TopOnly), *DoneWords));

	TestEqual(TEXT("**the three chances, in their order**"), UCataclysmForge::AffixNodesChanceText(All),
			  FString(TEXT(" The empire tree's affix nodes: a 40% chance of one more affix; a 10% chance one affix rises a "
						   "tier; a 100% chance one affix's roll is made its best.")));
	TestEqual(TEXT("only the nodes held are named"), UCataclysmForge::AffixNodesChanceText(Draft),
			  FString(TEXT(" The empire tree's affix nodes: a 40% chance of one more affix.")));
	TestEqual(TEXT("CONTROL: none held, no words"), UCataclysmForge::AffixNodesChanceText(FCataclysmForgeAffixNodes()),
			  FString());
	TestEqual(TEXT("**both roll nodes**"), UCataclysmForge::RollsText(Both),
			  FString(TEXT(" Its roll is drawn from 30 percent up, with a 45% chance from 75 percent up.")));
	TestEqual(TEXT("the floor alone"), UCataclysmForge::RollsText(FloorOnly),
			  FString(TEXT(" Its roll is drawn from 30 percent up.")));
	TestEqual(TEXT("Precision Crafting alone"), UCataclysmForge::RollsText(TopOnly),
			  FString(TEXT(" Its roll has a 45% chance to be drawn from 75 percent up.")));
	TestEqual(TEXT("CONTROL: neither, no words"), UCataclysmForge::RollsText(FCataclysmForgeRolls()), FString());
	if (TestNotNull(TEXT("set-up: the affix table"), AffixTable))
	{
		TestEqual(TEXT("**what was done, in order**"), DoneWords,
				  FString(TEXT("added Flat armor at tier 2; made Flat block chance's roll its best, 37 to 100 percent")));
	}
	TestEqual(TEXT("**the play loop line**"),
			  FCataclysmPlayLoopLog::ForgeAffixNodesLine(TEXT("Quality Helm"), TEXT("added Flat armor at tier 2")),
			  FString(TEXT("the empire tree's affix nodes, at the forge on Quality Helm: added Flat armor at tier 2")));
	TestEqual(TEXT("CONTROL: nothing done, no words"),
			  UCataclysmForge::AffixNodesDoneText(FCataclysmForgeAffixNodesDone(), AffixTable), FString());
	return true;
}

#undef CATACLYSM_AFFIX_NODES_TEST

#endif // WITH_AUTOMATION_TESTS
