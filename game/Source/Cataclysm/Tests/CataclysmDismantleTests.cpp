// Copyright Stephen Dubois. All Rights Reserved.

/**
 * DISMANTLING A CARRIED PIECE AT THE FORGE. Issue #2848, slice S2, ruled by the coordinating session on 2026-11-22: the
 * plan of selling and dismantling, sections 6 to 8, and D1 to D5.
 *
 * A dismantle is instant: no gold, no days, no residue, no material (D2). It destroys the piece and gives the socketed
 * gems and the refund, one upgrade stone of each level raised through, which are certain; and Schematic Fragments by
 * rarity (N4), Dismantling Dust a socket (N5) and base upgrade stones by rarity (section 8), each drawn as its whole part
 * and one more with its fraction's chance (D5), raised by Salvage Protocol and Disassembly Expert (N6, N7), which never
 * raise the refund. A full gear tab does not refuse it (#2849; D4 until then). An item records the gear level and the tier it arrived at, and the
 * character and run records went to versions 15 and 18 to carry them (ruled 2026-11-24; DM11 and DM12).
 *
 * EVERY TEST READS THE TABLES THE GAME LOADS. The scene is the one `CataclysmForgeEnchantmentsTests.cpp` builds, renamed
 * for this file because the module is built in unity files and the engine declares a global `FScene`.
 */

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmGems.h"
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
#include "UObject/Package.h"

namespace CataclysmDismantleTest
{
	const FName Fragments(UCataclysmForge::SchematicFragmentsRow);
	const FName Dust(UCataclysmForge::DismantlingDustRow);
	const FName OfLife(TEXT("Gem_Of_Life"));
	const FName SalvageProtocol(TEXT("basic_00000055"));		// Artisan, 10 points
	const FName DisassemblyExpert(TEXT("keystone_00000056"));	// Artisan, 1 point

	FCataclysmRolledAffix Affix(const TCHAR* Row)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Row);
		Out.Tier = 2;
		Out.Roll = 0.5f;
		return Out;
	}

	/**
	 * A Masterful helm, four affixes, at gear level `Level` having arrived at `AtDrop` at tier 1, residue 150, with one
	 * Defense socket holding Of Life (Superb) when `bGem`.
	 */
	FCataclysmItem MasterfulHelm(int32 Level, int32 AtDrop, bool bGem = true)
	{
		FCataclysmItem Out;
		Out.Base = FName(TEXT("Head_Helm"));
		Out.GearLevel = Level;
		Out.GearLevelAtDrop = AtDrop;
		Out.TierAtDrop = 1;
		Out.Residue = 150.0f;
		Out.Affixes = {Affix(TEXT("Stat_Flat_maximum_health")), Affix(TEXT("Stat_Flat_armor")),
					   Affix(TEXT("Stat_Flat_block_chance")), Affix(TEXT("Stat_Increased_maximum_health"))};
		FCataclysmSocket& Socket = Out.Sockets.AddDefaulted_GetRef();
		Socket.Type = ECataclysmSocketType::Defense;
		if (bGem)
		{
			Socket.Gem = OfLife;
			Socket.GemRarity = ECataclysmRarity::Superb;
		}
		return Out;
	}

	/** The same helm made Legendary: three affixes and one enchantment pair. */
	FCataclysmItem LegendaryHelm(int32 Level, int32 AtDrop)
	{
		FCataclysmItem Out = MasterfulHelm(Level, AtDrop, /*bGem=*/false);
		Out.Affixes.SetNum(3);
		FCataclysmRolledEnchantment Pair;
		Pair.Positive = FName(TEXT("Positive_Gain_10_30_increased_health"));
		Pair.Negative = FName(TEXT("Negative_You_have_no_armor"));
		Out.Enchantments = {Pair};
		Out.EnchantmentCount = 1;
		return Out;
	}

	TArray<FString> StoneLevels(const TArray<FName>& Stones)
	{
		TArray<FString> Out;
		for (const FName& Stone : Stones)
		{
			Out.Add(Stone.ToString());
		}
		return Out;
	}

	bool TheTables(FAutomationTestBase& Test)
	{
		return Test.TestTrue(TEXT("set-up: the generated base, affix, gear rarity, material tier, crafting material and gem "
								  "tables"),
							 UCataclysmItemModifiers::LoadBaseTable() && UCataclysmDropRoll::LoadAffixTable()
								 && UCataclysmDropRoll::LoadGearRarityTable() && UCataclysmDropRoll::LoadMaterialTierTable()
								 && UCataclysmDropRoll::LoadCraftingMaterialTable() && UCataclysmGems::LoadGeneratedTable());
	}

	struct FDismantleScene
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
	};

	/** A world with this project's game instance, a possessed player with a million gold of its own, and a run. */
	FDismantleScene MakeScene()
	{
		FDismantleScene Out;
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
			// SOLO SELF-FOUND, WITH GOLD OF ITS OWN, so no account record is written.
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Dismantler"), Partition);
			Out.State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2848, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// DM1. The plan: the gems and the refund, certain; the chances by rarity; the refusals.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantlePlanTest,
	"Cataclysm.Dismantle.ADismantleGivesTheGemsTheRefundAndChancesByRarityAndRefusesWhatItCannot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Masterful helm raised from +0 to +4 with Of Life (Superb) in its one socket: the gem, one stone each of +1 to +4,
 * 0.06 fragments, 0.02 Dust and 0.025 stones expected. A Legendary that dropped at +4 refunds nothing; raised to +6 it
 * refunds +5 and +6; saved before the field (-1) it is read at its rarity's gate, +4, and refunds the same. 0.1 fragments
 * and 0.05 stones for a Legendary. REFUSED: no piece, the weapon made at creation, a piece with no rarity, no gear rarity
 * table. Prints "DISMANTLE PLAN".
 */
bool FCataclysmDismantlePlanTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const FCataclysmForgeDismantle Plan = UCataclysmForge::DismantleOf(MasterfulHelm(4, 0), 0.0f, 0.0f, Rarities);
	TestTrue(TEXT("a carried Masterful helm can be dismantled"), Plan.bPossible);
	TestTrue(TEXT("Masterful"), Plan.Rarity == ECataclysmRarity::Masterful);
	TestEqual(TEXT("0.06 Schematic Fragments expected"), Plan.FragmentsExpected, 0.06f, 0.00001f);
	TestEqual(TEXT("0.02 Dismantling Dust for its one socket"), Plan.DustExpected, 0.02f, 0.00001f);
	TestEqual(TEXT("0.025 base stones"), Plan.StonesExpected, 0.025f, 0.00001f);
	TestEqual(TEXT("the refund: one stone each of +1 to +4"), StoneLevels(Plan.RefundStones),
			  TArray<FString>{TEXT("Material_Upgrade_Stone_1"), TEXT("Material_Upgrade_Stone_2"),
							  TEXT("Material_Upgrade_Stone_3"), TEXT("Material_Upgrade_Stone_4")});
	TestTrue(TEXT("Of Life (Superb), back to the bag"), Plan.Gems.Num() == 1 && Plan.Gems[0].Key == OfLife
		&& Plan.Gems[0].Value == ECataclysmRarity::Superb);
	TestFalse(TEXT("nothing drawn in a plan"), Plan.bDrawn);
	TestEqual(TEXT("the first press's question"),
			  UCataclysmForge::DismantleQuestion(Plan, TEXT("Masterful Helm of Warding")),
			  FString(TEXT("Press again to dismantle Masterful Helm of Warding: it is destroyed, and gives back 1 gem and 4 "
						   "upgrade stones (+1, +2, +3, +4), and about 0.06 Schematic Fragments, 0.02 Dismantling Dust and "
						   "0.03 upgrade stones drawn at random. No gold, no days.")));

	const FCataclysmForgeDismantle Dropped = UCataclysmForge::DismantleOf(LegendaryHelm(4, 4), 0.0f, 0.0f, Rarities);
	TestEqual(TEXT("a Legendary that dropped at +4 refunds nothing"), Dropped.RefundStones.Num(), 0);
	TestEqual(TEXT("0.1 fragments for a Legendary"), Dropped.FragmentsExpected, 0.1f, 0.00001f);
	TestEqual(TEXT("0.05 stones for a Legendary"), Dropped.StonesExpected, 0.05f, 0.00001f);
	const FCataclysmForgeDismantle Raised = UCataclysmForge::DismantleOf(LegendaryHelm(6, 4), 0.0f, 0.0f, Rarities);
	TestEqual(TEXT("raised from +4 to +6, it refunds +5 and +6"), StoneLevels(Raised.RefundStones),
			  TArray<FString>{TEXT("Material_Upgrade_Stone_5"), TEXT("Material_Upgrade_Stone_6")});
	const FCataclysmForgeDismantle Unknown = UCataclysmForge::DismantleOf(LegendaryHelm(6, -1), 0.0f, 0.0f, Rarities);
	TestEqual(TEXT("saved before the field, read at the Legendary gate +4: +5 and +6"), StoneLevels(Unknown.RefundStones),
			  StoneLevels(Raised.RefundStones));
	TestEqual(TEXT("from level 4"), Unknown.RefundFromLevel, 4);

	FCataclysmItem Creation = MasterfulHelm(4, 0);
	Creation.bMadeAtCreation = true;
	TestEqual(TEXT("the weapon made at creation"), UCataclysmForge::DismantleOf(Creation, 0.0f, 0.0f, Rarities).Refusal,
			  FString(TEXT("The weapon made at creation is the character's own; the forge does not dismantle it.")));
	FCataclysmItem NoRarity = MasterfulHelm(0, 0, false);
	NoRarity.Affixes.Reset();
	TestEqual(TEXT("no rarity"), UCataclysmForge::DismantleOf(NoRarity, 0.0f, 0.0f, Rarities).Refusal,
			  FString(TEXT("That piece has no rarity, so there is nothing to dismantle it into.")));
	TestEqual(TEXT("no piece"), UCataclysmForge::DismantleOf(FCataclysmItem(), 0.0f, 0.0f, Rarities).Refusal,
			  FString(TEXT("There is nothing there to dismantle.")));
	TestEqual(TEXT("no gear rarity table"), UCataclysmForge::DismantleOf(MasterfulHelm(4, 0), 0.0f, 0.0f, nullptr).Refusal,
			  FString(TEXT("The gear rarity table could not be read, so what the piece was raised through is not known.")));
	AddInfo(FString::Printf(TEXT("DISMANTLE PLAN: %d refund, %d gem; %.3f / %.3f / %.3f; Legendary raised %d"),
							Plan.RefundStones.Num(), Plan.Gems.Num(), Plan.FragmentsExpected, Plan.DustExpected,
							Plan.StonesExpected, Raised.RefundStones.Num()));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM2. The two nodes raise the drawn yields, from Superb up for Disassembly Expert, and never the refund.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleIncreaseTest,
	"Cataclysm.Dismantle.SalvageProtocolAndDisassemblyExpertRaiseTheDrawnYieldsAndNeverTheRefund",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Salvage Protocol at 10 points (0.25) and Disassembly Expert (0.20) add to 0.45 on a Masterful piece and to 0.25 on a
 * Quality one, which Disassembly Expert does not reach (O4). The Masterful helm's fragments 0.06 x 1.45, Dust 0.02 x
 * 1.45 and stones 0.025 x 1.45; its refund the same four stones. Prints "DISMANTLE INCREASE".
 */
bool FCataclysmDismantleIncreaseTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	if (!TheTables(*this))
	{
		return false;
	}
	TestEqual(TEXT("Masterful: both"), UCataclysmForge::DismantleIncreaseFor(ECataclysmRarity::Masterful, 0.25f, 0.20f),
			  0.45f, 0.00001f);
	TestEqual(TEXT("Superb, the lowest rung Disassembly Expert reaches"),
			  UCataclysmForge::DismantleIncreaseFor(ECataclysmRarity::Superb, 0.0f, 0.20f), 0.20f, 0.00001f);
	TestEqual(TEXT("Quality: Salvage Protocol only"),
			  UCataclysmForge::DismantleIncreaseFor(ECataclysmRarity::Quality, 0.25f, 0.20f), 0.25f, 0.00001f);
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const FCataclysmForgeDismantle Plain = UCataclysmForge::DismantleOf(MasterfulHelm(4, 0), 0.0f, 0.0f, Rarities);
	const FCataclysmForgeDismantle Raised = UCataclysmForge::DismantleOf(MasterfulHelm(4, 0), 0.25f, 0.20f, Rarities);
	AddInfo(FString::Printf(TEXT("DISMANTLE INCREASE: %.4f / %.4f / %.4f against %.4f / %.4f / %.4f; refund %d and %d"),
							Raised.FragmentsExpected, Raised.DustExpected, Raised.StonesExpected, Plain.FragmentsExpected,
							Plain.DustExpected, Plain.StonesExpected, Raised.RefundStones.Num(), Plain.RefundStones.Num()));
	TestEqual(TEXT("fragments x 1.45"), Raised.FragmentsExpected, 0.06f * 1.45f, 0.00001f);
	TestEqual(TEXT("Dust x 1.45"), Raised.DustExpected, 0.02f * 1.45f, 0.00001f);
	TestEqual(TEXT("stones x 1.45"), Raised.StonesExpected, 0.025f * 1.45f, 0.00001f);
	TestEqual(TEXT("the refund is never raised"), StoneLevels(Raised.RefundStones), StoneLevels(Plain.RefundStones));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM3. A fractional yield: the whole part always, one more with the fraction's chance.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleFractionTest,
	"Cataclysm.Dismantle.AFractionalYieldGivesItsWholePartAndOneMoreWithItsChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Over 4,000 seeds: 2.0 always gives 2, 0 always 0, and 1.25 gives 1 or 2, 2 about a quarter of the time (within 0.03).
 * Prints "DISMANTLE FRACTION".
 */
bool FCataclysmDismantleFractionTest::RunTest(const FString&)
{
	int32 Whole = 0;
	int32 None = 0;
	int32 Outside = 0;
	int32 Extra = 0;
	constexpr int32 Seeds = 4000;
	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		FRandomStream A(Seed);
		Whole += UCataclysmForge::DrawWholeAndFraction(2.0f, A) == 2 ? 1 : 0;
		FRandomStream B(Seed);
		None += UCataclysmForge::DrawWholeAndFraction(0.0f, B) == 0 ? 1 : 0;
		FRandomStream C(Seed);
		const int32 Drawn = UCataclysmForge::DrawWholeAndFraction(1.25f, C);
		Outside += (Drawn != 1 && Drawn != 2) ? 1 : 0;
		Extra += Drawn == 2 ? 1 : 0;
	}
	const float Share = static_cast<float>(Extra) / static_cast<float>(Seeds);
	AddInfo(FString::Printf(TEXT("DISMANTLE FRACTION: 2.0 gave 2 %d times, 0 gave 0 %d times, 1.25 gave 2 in %.4f"), Whole,
							None, Share));
	TestEqual(TEXT("2.0 always gives 2"), Whole, Seeds);
	TestEqual(TEXT("0 always gives 0"), None, Seeds);
	TestEqual(TEXT("1.25 gives only 1 or 2"), Outside, 0);
	TestEqual(TEXT("and 2 about a quarter of the time"), Share, 0.25f, 0.03f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM4. A base stone: a material tier's stone, held at the cap of the piece's tier.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleStoneTest,
	"Cataclysm.Dismantle.ABaseStoneIsAMaterialTiersStoneHeldAtThePiecesTierCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A plan with exactly one base stone, drawn on 2,000 seeds: at tier 1 every stone is +1 to +3 (the cap, tier + 2), and
 * +1 and +2 together are most of them (material tier 1 is the commonest); at tier 8 stones above +3 come too. The
 * piece's own `TierAtDrop` is the tier, and a piece with none reads the run's. Prints "DISMANTLE STONES".
 */
bool FCataclysmDismantleStoneTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Tiers = UCataclysmDropRoll::LoadMaterialTierTable();
	int32 AboveCapAtOne = 0;
	int32 LowAtOne = 0;
	int32 AboveThreeAtEight = 0;
	int32 Drawn = 0;
	for (int32 Seed = 1; Seed <= 2000; ++Seed)
	{
		for (const int32 Tier : {1, 8})
		{
			FCataclysmForgeDismantle Plan;
			Plan.StonesExpected = 1.0f;
			FRandomStream Stream(Seed);
			UCataclysmForge::DrawDismantle(Plan, Tiers, /*MagicFind=*/0.0f, Tier, Stream);
			if (Plan.BaseStones.Num() != 1)
			{
				continue;
			}
			++Drawn;
			const FString Row = Plan.BaseStones[0].ToString();
			const int32 Level = FCString::Atoi(*Row.RightChop(FString(TEXT("Material_Upgrade_Stone_")).Len()));
			if (Tier == 1)
			{
				AboveCapAtOne += Level > 3 ? 1 : 0;
				LowAtOne += Level <= 2 ? 1 : 0;
			}
			else
			{
				AboveThreeAtEight += Level > 3 ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("DISMANTLE STONES: %d drawn; tier 1 above +3 %d, +1 or +2 %d of 2000; tier 8 above +3 %d"),
							Drawn, AboveCapAtOne, LowAtOne, AboveThreeAtEight));
	TestEqual(TEXT("one stone on every draw"), Drawn, 4000);
	TestEqual(TEXT("never above +3 at tier 1"), AboveCapAtOne, 0);
	TestTrue(TEXT("+1 and +2 the most at tier 1"), LowAtOne > 1000);
	TestTrue(TEXT("control: above +3 at tier 8"), AboveThreeAtEight > 0);
	FCataclysmItem Unknown = MasterfulHelm(4, 0);
	Unknown.TierAtDrop = 0;
	TestEqual(TEXT("a piece's own tier"), UCataclysmForge::DismantleStoneTier(MasterfulHelm(4, 0), 6), 1);
	TestEqual(TEXT("and the run's for a piece with none"), UCataclysmForge::DismantleStoneTier(Unknown, 6), 6);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM5. The craft on a carried piece.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleCraftTest,
	"Cataclysm.Dismantle.TheCraftDestroysTheCarriedPieceAndBagsWhatItGaveForNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Masterful helm, +4 from +0, with Of Life: done; its cell empty; Of Life (Superb) and one stone each of +1 to +4
 * in the bag, besides what the day drew, which is the draw `DrawDismantle` gives on the same seed, so a reload draws the
 * same; no gold paid and no day passed. Prints "DISMANTLED".
 */
bool FCataclysmDismantleCraftTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FDismantleScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmItem Helm = MasterfulHelm(4, 0);
	const int32 Cell = Scene.Bag()->AddItem(Helm);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	float MagicFind = 0.0f;
	float LootQuantity = 0.0f;
	UCataclysmDropSpawner::LootStatsOf(Scene.Character, MagicFind, LootQuantity);
	FCataclysmForgeDismantle Expected =
		UCataclysmForge::DismantleOf(Helm, 0.0f, 0.0f, UCataclysmDropRoll::LoadGearRarityTable());
	FRandomStream Stream(UCataclysmForge::AffixCraftSeed(Helm, UCataclysmForge::DismantleSalt, 0, Scene.Run->WalkSeedMask,
														 DayBefore));
	UCataclysmForge::DrawDismantle(Expected, UCataclysmDropRoll::LoadMaterialTierTable(), MagicFind,
								   UCataclysmForge::DismantleStoneTier(Helm, Scene.Run->DifficultyTier), Stream);

	FCataclysmForgeDismantle Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Dismantle(Scene.World, FCataclysmForgeTarget::Carried(Cell), Done, Message);
	TestEqual(TEXT("the dismantle is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (Result != ECataclysmForgeResult::Done)
	{
		AddError(Message);
		return false;
	}
	TestNull(TEXT("the piece is gone from its cell"), Scene.Bag()->ItemAt(Cell));
	TestEqual(TEXT("Of Life (Superb) is back in the bag"), Scene.Bag()->CountOfGems(OfLife, ECataclysmRarity::Superb), 1);
	TestTrue(TEXT("what was drawn is the day's draw"), Done.Fragments == Expected.Fragments && Done.Dust == Expected.Dust
		&& StoneLevels(Done.BaseStones) == StoneLevels(Expected.BaseStones));
	TestEqual(TEXT("the fragments drawn are in the bag"), Scene.Bag()->CountOfMaterial(Fragments), Done.Fragments);
	TestEqual(TEXT("the Dust drawn is in the bag"), Scene.Bag()->CountOfMaterial(Dust), Done.Dust);
	for (int32 Level = 1; Level <= 4; ++Level)
	{
		const FName Stone = UCataclysmForge::StoneFor(Level);
		const int32 Base = Done.BaseStones.FilterByPredicate([&Stone](const FName& Each) { return Each == Stone; }).Num();
		TestEqual(FString::Printf(TEXT("one +%d stone refunded, and %d drawn"), Level, Base),
				  Scene.Bag()->CountOfMaterial(Stone), 1 + Base);
	}
	TestEqual(TEXT("no day passed"), Scene.Run->Day(), DayBefore);
	TestEqual(TEXT("no gold paid"), Scene.State->GetOwnGold(), GoldBefore);
	AddInfo(FString::Printf(TEXT("DISMANTLED: %s; day %d"), *UCataclysmForge::DismantleGiven(Done), Scene.Run->Day()));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM6. A full gear tab: the dismantle is done, and what it gives back is taken (#2849; refused before it, D4).
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleFullBagTest,
	"Cataclysm.Dismantle.AFullGearTabStillDismantlesAndTakesWhatItGivesBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The helm in a bag whose other 47 gear slots hold other pieces. Until #2849 a full bag refused, "Make room in the bag
 * first." (D4), because the helm gives back Of Life and four stones the bag held no stack of. Since #2849 materials and
 * gems go to stores that never refuse, so it is done: the helm leaves its cell and Of Life (Superb) is carried. THE
 * CONTROL: the other 47 pieces are all still carried. Prints "FULL BAG".
 */
bool FCataclysmDismantleFullBagTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FDismantleScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(MasterfulHelm(4, 0));
	TArray<int32> Others;
	for (int32 Cell2 = Scene.Bag()->AddItem(MasterfulHelm(0, 0, false)); Cell2 != INDEX_NONE;
		 Cell2 = Scene.Bag()->AddItem(MasterfulHelm(0, 0, false)))
	{
		Others.Add(Cell2);
	}
	if (!TestEqual(TEXT("set-up: the gear tab is full"), Scene.Bag()->NumFreeSlots(), 0)
		|| !TestEqual(TEXT("set-up: and holds 47 other pieces"), Others.Num(), 47))
	{
		return false;
	}
	FCataclysmForgeDismantle Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Dismantle(Scene.World, FCataclysmForgeTarget::Carried(Cell), Done, Message);
	AddInfo(FString::Printf(TEXT("FULL BAG: '%s'; %s; %d gear slots free"), *Message, *UCataclysmForge::DismantleGiven(Done),
							Scene.Bag()->NumFreeSlots()));
	TestEqual(TEXT("**the dismantle is done with every gear slot full**"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestNull(TEXT("**the helm has left its cell**"), Scene.Bag()->ItemAt(Cell));
	TestEqual(TEXT("**and Of Life (Superb) is carried**"), Scene.Bag()->CountOfGems(OfLife, ECataclysmRarity::Superb), 1);
	TestEqual(TEXT("one gear slot is free: the helm's"), Scene.Bag()->NumFreeSlots(), 1);
	int32 StillCarried = 0;
	for (const int32 Each : Others)
	{
		StillCarried += Scene.Bag()->ItemAt(Each) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("CONTROL: the other 47 pieces are all still carried"), StillCarried, 47);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM7. A drop records where it arrived.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleDropRecordsTest,
	"Cataclysm.Dismantle.ADropRecordsTheGearLevelAndTierItArrivedAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two hundred drops at tiers 1 and 8: each records its gear level as `GearLevelAtDrop`, its rarity's gate, and the tier
 * it dropped at. Prints "DROPS RECORD".
 */
bool FCataclysmDismantleDropRecordsTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	int32 Rolled = 0;
	int32 Wrong = 0;
	int32 Raised = 0;
	for (int32 Seed = 1; Seed <= 100; ++Seed)
	{
		for (const int32 Tier : {1, 8})
		{
			FRandomStream Stream(Seed * 31 + Tier);
			const FString Slot = UCataclysmDropRoll::RollSlot(Bases, Stream);
			FCataclysmItem Item;
			if (!UCataclysmDropRoll::RollItem(Bases, UCataclysmDropRoll::LoadAffixTable(),
											  UCataclysmDropRoll::LoadGearRarityTable(),
											  UCataclysmDropRoll::LoadItemSocketTable(),
											  UCataclysmDropRoll::LoadAffixTierTable(),
											  UCataclysmWeaponSkills::LoadGeneratedTable(),
											  UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
											  UCataclysmDropRoll::LoadNegativeEnchantmentTable(), Slot, Tier,
											  /*MagicFind=*/0.0f, Stream, Item, /*bChaotic=*/false))
			{
				continue;
			}
			++Rolled;
			Wrong += (Item.GearLevelAtDrop != Item.GearLevel || Item.TierAtDrop != Tier) ? 1 : 0;
			Raised += Item.GearLevel > 0 ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("DROPS RECORD: %d rolled, %d wrong, %d above +0"), Rolled, Wrong, Raised));
	TestEqual(TEXT("every drop rolled"), Rolled, 200);
	TestEqual(TEXT("each records its gear level and its tier"), Wrong, 0);
	TestTrue(TEXT("control: some arrived above +0, so the level is not always 0"), Raised > 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM8. The two Artisan nodes add up to their texts.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleNodesTest,
	"Cataclysm.Dismantle.TheTwoArtisanNodesAddUpToTheirTexts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Salvage Protocol at its 10 points, "25% more crafting materials", and Disassembly Expert, "+20%", read through
 * `UCataclysmEmpireTree::PlayerBonusesFor` from the tables the game loads: 0.25 and 0.20. At 4 points Salvage Protocol
 * gives 0.10. Prints "ARTISAN NODES".
 */
bool FCataclysmDismantleNodesTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	if (!TestTrue(TEXT("set-up: the empire tree's node and effect tables"),
				  UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(SalvageProtocol, 10);
	Tree.Add(DisassemblyExpert, 1);
	const FCataclysmEmpireTreePlayerBonuses Held =
		UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(), UCataclysmEmpireTree::LoadEffectTable(),
											   Tree);
	FCataclysmPassiveAllocation Four;
	Four.Add(SalvageProtocol, 4);
	const FCataclysmEmpireTreePlayerBonuses AtFour =
		UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(), UCataclysmEmpireTree::LoadEffectTable(),
											   Four);
	AddInfo(FString::Printf(TEXT("ARTISAN NODES: %.4f and %.4f; at 4 points %.4f"), Held.DismantleMaterialsIncreased,
							Held.DismantleMaterialsIncreasedSuperbAndAbove, AtFour.DismantleMaterialsIncreased));
	TestEqual(TEXT("Salvage Protocol at 10 points: 25%"), Held.DismantleMaterialsIncreased, 0.25f, 0.0001f);
	TestEqual(TEXT("Disassembly Expert: 20%"), Held.DismantleMaterialsIncreasedSuperbAndAbove, 0.20f, 0.0001f);
	TestEqual(TEXT("Salvage Protocol at 4 points: 10%"), AtFour.DismantleMaterialsIncreased, 0.10f, 0.0001f);
	TestEqual(TEXT("control: no Disassembly Expert at 4 points of Salvage Protocol alone"),
			  AtFour.DismantleMaterialsIncreasedSuperbAndAbove, 0.0f, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM9. The seed text: where a piece arrived, only when known.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleSeedTextTest,
	"Cataclysm.Dismantle.TheSeedTextAddsWhereAPieceArrivedOnlyWhenKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A piece holding the two fields' defaults writes the seed text it wrote before they existed, so no pinned forge draw
 * moves; one that dropped at +4 at tier 3 adds ";AtDrop=4:3", so two pieces differing only there draw apart. Prints
 * "SEED TEXT".
 */
bool FCataclysmDismantleSeedTextTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	FCataclysmItem Default = MasterfulHelm(4, -1);
	Default.TierAtDrop = 0;
	FCataclysmItem Known = Default;
	Known.GearLevelAtDrop = 4;
	Known.TierAtDrop = 3;
	const FString DefaultText = UCataclysmForge::ItemSeedText(Default);
	const FString KnownText = UCataclysmForge::ItemSeedText(Known);
	AddInfo(FString::Printf(TEXT("SEED TEXT: ...%s / ...%s"), *DefaultText.Right(30), *KnownText.Right(40)));
	TestFalse(TEXT("the defaults add nothing"), DefaultText.Contains(TEXT("AtDrop")));
	TestTrue(TEXT("a known arrival is added"), KnownText.EndsWith(TEXT(";AtDrop=4:3")));
	TestEqual(TEXT("and is the default text with it"), KnownText, DefaultText + TEXT(";AtDrop=4:3"));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM10. The forge screen's button.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleScreenTest,
	"Cataclysm.Dismantle.TheScreenOffersADismantleOnlyForACarriedPieceItCanTake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A carried Masterful helm is offered "Dismantle this piece (it is destroyed)"; the same helm worn, the weapon made at
 * creation and a piece with no rarity are offered none. Prints "DISMANTLE SCREEN".
 */
bool FCataclysmDismantleScreenTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleTest;
	FCataclysmItem Creation = MasterfulHelm(4, 0);
	Creation.bMadeAtCreation = true;
	FCataclysmItem NoRarity = MasterfulHelm(0, 0, false);
	NoRarity.Affixes.Reset();
	const FString Carried = UCataclysmForgeLayout::DismantleLabelFor(MasterfulHelm(4, 0), /*bWorn=*/false);
	AddInfo(FString::Printf(TEXT("DISMANTLE SCREEN: '%s'"), *Carried));
	TestEqual(TEXT("a carried Masterful helm"), Carried, FString(TEXT("Dismantle this piece (it is destroyed)")));
	TestTrue(TEXT("none worn"), UCataclysmForgeLayout::DismantleLabelFor(MasterfulHelm(4, 0), /*bWorn=*/true).IsEmpty());
	TestTrue(TEXT("none for the weapon made at creation"),
			 UCataclysmForgeLayout::DismantleLabelFor(Creation, /*bWorn=*/false).IsEmpty());
	TestTrue(TEXT("none for a piece with no rarity"), UCataclysmForgeLayout::DismantleLabelFor(NoRarity, false).IsEmpty());
	TestEqual(TEXT("the button's name"), UCataclysmForgeLayout::DismantleButtonName(), FName(TEXT("Dismantle")));
	return true;
}

namespace CataclysmDismantleSaveTest
{
	/** A committed file read through the whole chain, with its result, and the file's text for the comparison. */
	template <typename RecordType>
	const RecordType* ReadFixture(FAutomationTestBase& Test, const TCHAR* FileName, FString& OutText,
								  ECataclysmSaveLoadResult& OutResult)
	{
		FString Reason;
		if (!CataclysmSaveFixtures::Read(FileName, OutText, Reason))
		{
			Test.AddError(Reason);
			return nullptr;
		}
		FString Message;
		const RecordType* Read = Cast<RecordType>(FCataclysmSaveStorage::FromJson(
			OutText, RecordType::StaticClass(), GetTransientPackage(), OutResult, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s"), FileName, *Message));
		}
		return Read;
	}

	/** Where each item of a list arrived, "level:tier", in order. */
	FString ArrivalsOf(const TArray<const FCataclysmItem*>& Items)
	{
		TArray<FString> Out;
		for (const FCataclysmItem* Item : Items)
		{
			Out.Add(Item ? FString::Printf(TEXT("%d:%d"), Item->GearLevelAtDrop, Item->TierAtDrop) : TEXT("missing"));
		}
		return FString::Join(Out, TEXT(" "));
	}

	TArray<const FCataclysmItem*> CharacterItems(const UCataclysmCharacterSave& Character)
	{
		TArray<const FCataclysmItem*> Out;
		for (const FCataclysmCarriedSlot& Slot : Character.CarriedSlots)
		{
			Out.Add(&Slot.Item);
		}
		return Out;
	}

	TArray<const FCataclysmItem*> GroundItems(const UCataclysmRunSave& Run)
	{
		TArray<const FCataclysmItem*> Out;
		for (const FCataclysmSavedGroundItem& Ground : Run.Floor.GroundItems)
		{
			Out.Add(&Ground.Item);
		}
		return Out;
	}

	/** The older file at the current version with the step's "not known" on every item, against what the record writes. */
	bool KeepsEveryOtherField(const FString& OlderText, int32 VersionNow, void (*AddTheStep)(FJsonObject&),
							  const UCataclysmSaveRecord* Read, FString& OutWhere)
	{
		const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(OlderText);
		if (!Expected.IsValid())
		{
			OutWhere = TEXT("the older file is not a JSON object");
			return false;
		}
		Expected->SetNumberField(TEXT("SchemaVersion"), VersionNow);
		AddTheStep(*Expected);
		FString Rewritten;
		FString Error;
		return FCataclysmSaveStorage::ToJson(Read, Rewritten, Error)
			&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), OutWhere);
	}
}

// --------------------------------------------------------------------------------------------------------------------
// DM11. A character file from before an item recorded where it arrived migrates to "not known".
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleCharacterMigrationTest,
	"Cataclysm.Dismantle.ACharacterFileFromBeforeItemsRecordedWhereTheyArrivedMigratesToNotKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The character record went to version 15 for the two fields (ruled by the coordinating session on 2026-11-24, by
 * `game/Tests/SaveFixtures/README.md`). `Character_v14.json`, written before them, migrates: each carried item reads
 * -1 and 0, "not known", and every other field is kept. CONTROL: the committed character file is read, not migrated,
 * and its carried Circlet states +4 at tier 2, not the defaults, so the fields are read from the file: `Character_v17.json`
 * since issue #837 slice 1 and `Character_v16.json` before it, since issue #2849, which moved the inventory's stacks to tabs of their own (`Character_v15.json` from issue #2848), so
 * `Character_v14.json` also passes through that step, and the expected record takes both steps' defaults. Prints
 * "ARRIVAL CHARACTER".
 */
bool FCataclysmDismantleCharacterMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleSaveTest;
	FString OldText;
	FString NewText;
	ECataclysmSaveLoadResult OldResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult NewResult = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmCharacterSave* Old =
		ReadFixture<UCataclysmCharacterSave>(*this, TEXT("Character_v14.json"), OldText, OldResult);
	const UCataclysmCharacterSave* Committed =
		ReadFixture<UCataclysmCharacterSave>(*this, TEXT("Character_v17.json"), NewText, NewResult);
	if (!TestNotNull(TEXT("Character_v14.json loads"), Old) || !TestNotNull(TEXT("Character_v17.json loads"), Committed)
		|| !TestEqual(TEXT("set-up: Character_v14.json carries two slots"), Old->CarriedSlots.Num(), 2)
		|| !TestEqual(TEXT("set-up: Character_v17.json carries two slots"), Committed->CarriedSlots.Num(), 2))
	{
		return false;
	}
	FString Where;
	const bool bKept = KeepsEveryOtherField(OldText, UCataclysmCharacterSave::SchemaVersionNow,
											[](FJsonObject& Expected)
											{
												CataclysmSaveFixtures::AddTheCharactersVersion15Defaults(Expected);
												CataclysmSaveFixtures::AddTheCharactersVersion16Defaults(Expected); // And 16, issue #2849.
												CataclysmSaveFixtures::AddTheCharactersVersion17Defaults(Expected); // And 17, issue #837 slice 1: no remembered defaults.
											},
											Old, Where);
	AddInfo(FString::Printf(TEXT("ARRIVAL CHARACTER: Character_v14.json %s, carried %s, the rest %s; "
								 "Character_v17.json %s, carried %s"),
							FCataclysmSaveStorage::Describe(OldResult), *ArrivalsOf(CharacterItems(*Old)),
							bKept ? TEXT("kept") : *Where, FCataclysmSaveStorage::Describe(NewResult),
							*ArrivalsOf(CharacterItems(*Committed))));
	TestEqual(TEXT("Character_v14.json is migrated"), static_cast<int32>(OldResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("**each carried item arrived at a place not known**"), ArrivalsOf(CharacterItems(*Old)),
			  FString(TEXT("-1:0 -1:0")));
	TestTrue(TEXT("and every other field is kept"), bKept);
	TestEqual(TEXT("CONTROL: Character_v17.json is read, not migrated"), static_cast<int32>(NewResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("CONTROL: its Circlet arrived at +4 at tier 2, and its empty slot is not known, as it states"),
			  ArrivalsOf(CharacterItems(*Committed)), FString(TEXT("4:2 -1:0")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// DM12. A run file from before an item recorded where it arrived migrates to "not known".
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDismantleRunMigrationTest,
	"Cataclysm.Dismantle.ARunFileFromBeforeItemsRecordedWhereTheyArrivedMigratesToNotKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The run record went to version 18 for the two fields, as the character record went to 15. `Run_v17.json`, written
 * before them, migrates: each ground item reads -1 and 0, "not known", and every other field is kept. CONTROL:
 * `Run_v18.json` is read, not migrated, and its Circlet on the ground states +0 at tier 4, not the defaults. Prints
 * "ARRIVAL RUN".
 */
bool FCataclysmDismantleRunMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmDismantleSaveTest;
	FString OldText;
	FString NewText;
	ECataclysmSaveLoadResult OldResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult NewResult = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmRunSave* Old = ReadFixture<UCataclysmRunSave>(*this, TEXT("Run_v17.json"), OldText, OldResult);
	const UCataclysmRunSave* Committed =
		ReadFixture<UCataclysmRunSave>(*this, TEXT("Run_v18.json"), NewText, NewResult);
	if (!TestNotNull(TEXT("Run_v17.json loads"), Old) || !TestNotNull(TEXT("Run_v18.json loads"), Committed)
		|| !TestEqual(TEXT("set-up: Run_v17.json has two ground items"), Old->Floor.GroundItems.Num(), 2)
		|| !TestEqual(TEXT("set-up: Run_v18.json has two ground items"), Committed->Floor.GroundItems.Num(), 2))
	{
		return false;
	}
	FString Where;
	const bool bKept = KeepsEveryOtherField(OldText, UCataclysmRunSave::SchemaVersionNow,
											&CataclysmSaveFixtures::AddTheRunsVersion18Defaults, Old, Where);
	AddInfo(FString::Printf(TEXT("ARRIVAL RUN: Run_v17.json %s, ground %s, the rest %s; Run_v18.json %s, ground %s"),
							FCataclysmSaveStorage::Describe(OldResult), *ArrivalsOf(GroundItems(*Old)),
							bKept ? TEXT("kept") : *Where, FCataclysmSaveStorage::Describe(NewResult),
							*ArrivalsOf(GroundItems(*Committed))));
	TestEqual(TEXT("Run_v17.json is migrated"), static_cast<int32>(OldResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("**each ground item arrived at a place not known**"), ArrivalsOf(GroundItems(*Old)),
			  FString(TEXT("-1:0 -1:0")));
	TestTrue(TEXT("and every other field is kept"), bKept);
	TestEqual(TEXT("CONTROL: Run_v18.json is read, not migrated"), static_cast<int32>(NewResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("CONTROL: its Circlet arrived at +0 at tier 4, and its gold pile is not known, as it states"),
			  ArrivalsOf(GroundItems(*Committed)), FString(TEXT("0:4 -1:0")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
