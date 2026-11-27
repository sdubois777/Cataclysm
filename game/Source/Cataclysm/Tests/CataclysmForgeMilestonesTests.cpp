// Copyright Stephen Dubois. All Rights Reserved.

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
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmGems.h"
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
 * THE FORGE'S TWO EMPIRE MILESTONE OPTIONS. Issue #47, slice F6, ruled by the coordinating session on 2026-11-22 (R1 to
 * R8).
 *
 * The Soul Forge (Edicts of Power's third): an upgrade has a 15% chance to add two levels, up to +10, and the second
 * costs nothing more. The Master Tinkerer (The Imperial Vanguard's second): an imprint or a change of drawback has a 5%
 * chance of a critical success, its rolls drawn twice with the better kept ("Lucky", R1), taking no days (R4). Each draws
 * on a stream of its own salt (R3), seeded by the item, so a test finds the item it wants by its kill count, which no
 * cost reads, AT THE DAY OF THE CRAFT, and works out what the draw gives from the seed rather than from the function
 * under test.
 *
 * WRITTEN BEFORE A BUILD. The scene is `CataclysmForgeEnchantmentsTests.cpp`'s, copied into this file's namespace and
 * named for it: a test type named `FScene` is ambiguous with the engine's.
 */
namespace CataclysmForgeMilestonesTest
{
	const FName Shard(UCataclysmForge::AetherialShardRow);
	const FName Essence(UCataclysmForge::RegretEssenceRow);
	const FName MoreHealth(TEXT("Positive_Gain_10_30_increased_health"));
	const FName NoArmour(TEXT("Negative_You_have_no_armor"));

	/** The two milestones and the option of each this slice builds. */
	const FName EdictsOfPower(TEXT("capstone_0000000n"));	// option 3, The Soul Forge
	const FName ImperialVanguard(TEXT("capstone_0000000o"));	// option 2, The Master Tinkerer
	const FName Foundations(TEXT("capstone_0000000m"));
	const FName ImperialZenith(TEXT("capstone_0000000p"));	// option 1, Imperial Prowess
	constexpr int32 SoulForgeOption = 3;
	constexpr int32 MasterTinkererOption = 2;

	/** A milestone's option chosen and its one point bought, by hand, as the Prowess tests take one. */
	void Take(FCataclysmPassiveAllocation& Tree, FName Milestone, int32 Option)
	{
		Tree.SetChosenOption(Milestone, Option);
		Tree.Add(Milestone, 1);
	}

	FCataclysmRolledAffix Affix(const TCHAR* Row)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Row);
		Out.Tier = 2;
		Out.Roll = 0.5f;
		return Out;
	}

	/** A helm at a gear level carrying 250 residue, with the kill count that picks its draws. */
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

	/** A Masterful sword at gear level 4, residue 150: slice F3's, with a kill count. */
	FCataclysmItem MasterfulSword(int32 Kills)
	{
		FCataclysmItem Out;
		Out.Base = FName(TEXT("Weapon_Sword"));
		Out.GearLevel = 4;
		Out.Residue = 150.0f;
		Out.Kills = Kills;
		Out.DamageTypes = {FName(TEXT("War"))};
		Out.Affixes = {Affix(TEXT("Stat_Flat_damage")), Affix(TEXT("Stat_Increased_damage")),
					   Affix(TEXT("Stat_Flat_critical_strike_chance")), Affix(TEXT("Stat_Increased_attack_speed"))};
		FCataclysmSocket& Socket = Out.Sockets.AddDefaulted_GetRef();
		Socket.Type = ECataclysmSocketType::Attack;
		return Out;
	}

	/** A Legendary helm at gear level 6, residue 200, holding More Health with No Armour: slice F3's, with a kill count. */
	FCataclysmItem LegendaryHelm(int32 Kills)
	{
		FCataclysmItem Out;
		Out.Base = FName(TEXT("Head_Helm"));
		Out.GearLevel = 6;
		Out.Residue = 200.0f;
		Out.Kills = Kills;
		Out.Affixes = {Affix(TEXT("Stat_Flat_maximum_health")), Affix(TEXT("Stat_Flat_armor")),
					   Affix(TEXT("Stat_Flat_block_chance"))};
		FCataclysmRolledEnchantment Held;
		Held.Positive = MoreHealth;
		Held.Negative = NoArmour;
		Held.PositiveRoll = 0.5f;
		Held.NegativeRoll = 0.5f;
		Out.Enchantments = {Held};
		Out.EnchantmentCount = 1;
		FCataclysmSocket& Socket = Out.Sockets.AddDefaulted_GetRef();
		Socket.Type = ECataclysmSocketType::Defense;
		return Out;
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

	/** The Soul Forge's draw, worked out from its seed and not from `SoulForgeLevels`. */
	float SoulForgeDraw(const FCataclysmItem& Item, int32 WalkSeedMask, int32 Day)
	{
		FRandomStream Stream(UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::SoulForgeSalt, Item.GearLevel,
															 WalkSeedMask, Day));
		return Stream.GetFraction();
	}

	/** The Master Tinkerer's three draws in order: the critical one, then a second positive and a second negative. */
	void CriticalDraws(const FCataclysmItem& Item, int32 Index, int32 WalkSeedMask, int32 Day, float& OutCritical,
					   float& OutPositive, float& OutNegative)
	{
		FRandomStream Stream(UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::EnchantCriticalSalt, Index,
															 WalkSeedMask, Day));
		OutCritical = Stream.GetFraction();
		OutPositive = Stream.GetFraction();
		OutNegative = Stream.GetFraction();
	}

	bool TheTables(FAutomationTestBase& Test)
	{
		return Test.TestTrue(TEXT("set-up: the generated base, affix, gear rarity, enchantment, effect, gem, weapon skill "
								  "and empire tree tables"),
							 UCataclysmItemModifiers::LoadBaseTable() && UCataclysmDropRoll::LoadAffixTable()
								 && UCataclysmDropRoll::LoadGearRarityTable()
								 && UCataclysmDropRoll::LoadPositiveEnchantmentTable()
								 && UCataclysmDropRoll::LoadNegativeEnchantmentTable()
								 && UCataclysmItemModifiers::LoadEnchantmentEffectTable()
								 && UCataclysmGems::LoadGeneratedTable() && UCataclysmWeaponSkills::LoadGeneratedTable()
								 && UCataclysmEmpireTree::LoadNodeTable() && UCataclysmEmpireTree::LoadEffectTable());
	}

	struct FMilestonesScene
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

		/** The solo character's own empire tree, holding `Option` of `Milestone` alone, or nothing for NAME_None. */
		void Hold(FName Milestone, int32 Option) const
		{
			FCataclysmPassiveAllocation& Tree = State->EditOwnEmpireTree();
			Tree = FCataclysmPassiveAllocation();
			if (!Milestone.IsNone())
			{
				Take(Tree, Milestone, Option);
			}
		}
	};

	/** A world with this project's game instance, a possessed solo player with a million gold of its own, and a run. */
	FMilestonesScene MakeScene()
	{
		FMilestonesScene Out;
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
			// SOLO SELF-FOUND: its empire tree is its own, on its player state, and it pays with gold of its own.
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Tinkerer"), Partition);
			Out.State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Out.Run->Begin(/* InSeed */ 4706, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}

	/** One upgrade of a fresh helm at `GearLevel` with `Kills`: its level after, the days, the residue and the gold. */
	struct FUpgraded
	{
		ECataclysmForgeResult Result = ECataclysmForgeResult::Refused;
		int32 Level = -1;
		int32 Days = -1;
		float Residue = -1.0f;
		int64 Gold = -1;
	};

	FUpgraded UpgradeOnce(const FMilestonesScene& Scene, int32 GearLevel, int32 Kills)
	{
		FUpgraded Out;
		const int32 Cell = Scene.Bag()->AddItem(UpgradeHelm(GearLevel, Kills));
		Scene.Bag()->AddMaterial(UCataclysmForge::StoneFor(GearLevel + 1), 2);
		const int32 DayBefore = Scene.Run->Day();
		const int64 GoldBefore = Scene.State->GetOwnGold();
		FCataclysmForgeUpgrade Done;
		FString Message;
		Out.Result = UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
		const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
		Out.Level = After ? After->GearLevel : -1;
		Out.Residue = After ? After->Residue : -1.0f;
		Out.Days = Scene.Run->Day() - DayBefore;
		Out.Gold = GoldBefore - Scene.State->GetOwnGold();
		if (After)
		{
			Scene.Bag()->RemoveItemAt(Cell);
		}
		return Out;
	}
}

#define CATACLYSM_MILESTONES_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// M1. The Soul Forge: two levels on a draw below 15%, at the +1's costs; one level above it, and one without the option.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A helm at +2 carrying 250 residue: an upgrade to +3 takes two +3 stones, adds 25 residue, 5 days (3 for the level, 2
 * for the residue) and 4,500 gold. CONTROL first, without the option, on a helm whose draw is below 0.15: +3. With the
 * option, at the day then: a helm whose draw is below 0.15 rises to +4 at those same costs (R2); one whose draw is not,
 * to +3. Prints "SOUL FORGE".
 */
CATACLYSM_MILESTONES_TEST(FCataclysmForgeMilestonesSoulForgeTest,
	"Cataclysm.ForgeMilestones.TheSoulForgeRaisesTwoLevelsOnADrawBelowFifteenPercent")
{
	using namespace CataclysmForgeMilestonesTest;
	FMilestonesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()) || !TheTables(*this))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const int32 Mask = Scene.Run->WalkSeedMask;
	auto Below = [&](int32 Kills) { return SoulForgeDraw(UpgradeHelm(2, Kills), Mask, Scene.Run->Day()) < 0.15f; };

	Scene.Hold(NAME_None, 0);
	const int32 ControlKills = KillsWhere(Below);
	const FUpgraded Control = UpgradeOnce(Scene, 2, ControlKills);

	Scene.Hold(EdictsOfPower, SoulForgeOption);
	const float Chance = UCataclysmGameInstance::EmpireTreePlayerBonusesFor(Scene.World).UpgradeExtraLevelChance;
	const int32 TwoKills = KillsWhere(Below);
	const FUpgraded Two = UpgradeOnce(Scene, 2, TwoKills);
	const int32 OneKills = KillsWhere([&](int32 Kills) { return !Below(Kills); });
	const FUpgraded One = UpgradeOnce(Scene, 2, OneKills);
	AddInfo(FString::Printf(TEXT("SOUL FORGE: chance %.2f; without the option (kills %d) +%d; with it, a draw below (kills "
								 "%d) +%d, %d days, residue %.1f, %lld gold; a draw above (kills %d) +%d"),
							Chance, ControlKills, Control.Level, TwoKills, Two.Level, Two.Days, Two.Residue, Two.Gold,
							OneKills, One.Level));

	if (!TestTrue(TEXT("set-up: a helm of each kind was found"),
				  ControlKills != INDEX_NONE && TwoKills != INDEX_NONE && OneKills != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the option gives 15%"), Chance, 0.15f, 0.0001f);
	TestEqual(TEXT("CONTROL: without the option, a draw below 15% raises one level"), Control.Level, 3);
	TestEqual(TEXT("**with it, a draw below 15% raises two: +2 to +4**"), Two.Level, 4);
	TestEqual(TEXT("at the +1's 5 days"), Two.Days, 5);
	TestEqual(TEXT("its 25 residue"), Two.Residue, 275.0f, 0.001f);
	TestEqual(TEXT("and its 4,500 gold (R2)"), Two.Gold, static_cast<int64>(4500));
	TestEqual(TEXT("**a draw at or above 15% raises one**"), One.Level, 3);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M2. The Soul Forge stops at +10.
// --------------------------------------------------------------------------------------------------------------------
/** A helm at +9 whose draw would be below 0.15, with the option: +10, not +11 (R7). Prints "SOUL FORGE AT TEN". */
CATACLYSM_MILESTONES_TEST(FCataclysmForgeMilestonesTenTest, "Cataclysm.ForgeMilestones.TheSoulForgeStopsAtTen")
{
	using namespace CataclysmForgeMilestonesTest;
	FMilestonesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()) || !TheTables(*this))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	Scene.Hold(EdictsOfPower, SoulForgeOption);
	const int32 Mask = Scene.Run->WalkSeedMask;
	const int32 Kills = KillsWhere(
		[&](int32 Each) { return SoulForgeDraw(UpgradeHelm(9, Each), Mask, Scene.Run->Day()) < 0.15f; });
	const FUpgraded Done = UpgradeOnce(Scene, 9, Kills);
	AddInfo(FString::Printf(TEXT("SOUL FORGE AT TEN: kills %d, result %d, +%d"), Kills, static_cast<int32>(Done.Result),
							Done.Level));

	TestTrue(TEXT("set-up: a helm whose draw is below 15% was found"), Kills != INDEX_NONE);
	TestEqual(TEXT("the upgrade is done"), static_cast<int32>(Done.Result), static_cast<int32>(ECataclysmForgeResult::Done));
	TestEqual(TEXT("**+9 rises to +10 and no further**"), Done.Level, UCataclysmItemValues::MaxGearLevel);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M3. The Master Tinkerer's imprint: the better of two rolls, and no days.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Slice F3's Masterful sword, giving up its second affix, on a day and kill count whose critical draw is below 0.05 and
 * whose second positive is higher and second negative lower than the plain draw's. With the option: the plain draw's
 * pair, its positive roll the second (higher) one and its negative the second (lower) one; no day passes; the residue,
 * the Shard and the gold are still taken. CONTROL, at the same day without the option, the same sword: the plain draw's
 * rolls, and days pass. Prints "TINKERER IMPRINT".
 */
CATACLYSM_MILESTONES_TEST(FCataclysmForgeMilestonesImprintTest,
	"Cataclysm.ForgeMilestones.TheMasterTinkerersImprintKeepsTheBetterOfTwoRollsAndTakesNoDays")
{
	using namespace CataclysmForgeMilestonesTest;
	FMilestonesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()) || !TheTables(*this))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const int32 Mask = Scene.Run->WalkSeedMask;
	const int32 Day = Scene.Run->Day();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Positives = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const UDataTable* Negatives = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	FCataclysmRolledEnchantment Plain;
	float Critical = 1.0f;
	float SecondPositive = 0.0f;
	float SecondNegative = 1.0f;
	const int32 Kills = KillsWhere([&](int32 Each)
	{
		const FCataclysmItem Sword = MasterfulSword(Each);
		FRandomStream Unused;
		if (!UCataclysmForge::DrawImprint(Sword, Bases, Positives, Negatives, Mask, Day, Plain, Unused))
		{
			return false;
		}
		CriticalDraws(Sword, Sword.Enchantments.Num(), Mask, Day, Critical, SecondPositive, SecondNegative);
		return Critical < 0.05f && SecondPositive > Plain.PositiveRoll && SecondNegative < Plain.NegativeRoll;
	});
	if (!TestTrue(TEXT("set-up: a sword whose draw is a critical success that changes both rolls was found"),
				  Kills != INDEX_NONE))
	{
		return false;
	}

	Scene.Hold(ImperialVanguard, MasterTinkererOption);
	const int32 Cell = Scene.Bag()->AddItem(MasterfulSword(Kills));
	Scene.Bag()->AddMaterial(Shard, 1);
	const int64 GoldBefore = Scene.State->GetOwnGold();
	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Imprint(Scene.World, FCataclysmForgeTarget::Carried(Cell), 1, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	const int32 DayAfter = Scene.Run->Day();
	const float ResidueAfter = After ? After->Residue : -1.0f;
	const int64 Paid = GoldBefore - Scene.State->GetOwnGold();

	// CONTROL: the same sword at the same day, without the option.
	Scene.Hold(NAME_None, 0);
	const int32 ControlCell = Scene.Bag()->AddItem(MasterfulSword(Kills));
	Scene.Bag()->AddMaterial(Shard, 1);
	FCataclysmForgeEnchantmentWork ControlDone;
	FString ControlMessage;
	const ECataclysmForgeResult ControlResult = UCataclysmForgeCraft::Imprint(
		Scene.World, FCataclysmForgeTarget::Carried(ControlCell), 1, false, ControlDone, ControlMessage);
	AddInfo(FString::Printf(TEXT("TINKERER IMPRINT: kills %d, critical %.4f; plain %s %.4f / %s %.4f; second %.4f / %.4f; "
								 "with the option %s %.4f / %s %.4f, %d days, residue %.1f, %lld gold '%s'; without it "
								 "%.4f / %.4f, %d days"),
							Kills, Critical, *Plain.Positive.ToString(), Plain.PositiveRoll, *Plain.Negative.ToString(),
							Plain.NegativeRoll, SecondPositive, SecondNegative, *Done.Drawn.Positive.ToString(),
							Done.Drawn.PositiveRoll, *Done.Drawn.Negative.ToString(), Done.Drawn.NegativeRoll,
							DayAfter - Day, ResidueAfter, Paid, *Message, ControlDone.Drawn.PositiveRoll,
							ControlDone.Drawn.NegativeRoll, ControlDone.Days));

	TestEqual(TEXT("the imprint is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	TestTrue(TEXT("the plain draw's pair"),
			 Done.Drawn.Positive == Plain.Positive && Done.Drawn.Negative == Plain.Negative);
	TestEqual(TEXT("**the positive roll, the higher of two**"), Done.Drawn.PositiveRoll, SecondPositive, 0.000001f);
	TestEqual(TEXT("**the negative roll, the lower of two**"), Done.Drawn.NegativeRoll, SecondNegative, 0.000001f);
	TestTrue(TEXT("the sword holds them"), After != nullptr && After->Enchantments.Num() == 1
										   && After->Enchantments[0].PositiveRoll == Done.Drawn.PositiveRoll
										   && After->Enchantments[0].NegativeRoll == Done.Drawn.NegativeRoll);
	TestEqual(TEXT("**no day passes (R4)**"), DayAfter, Day);
	TestEqual(TEXT("and the craft reports none"), Done.Days, 0);
	TestEqual(TEXT("the residue is still added: 150 to 155"), ResidueAfter, 155.0f, 0.001f);
	TestTrue(TEXT("and the gold paid"), Paid > 0);
	TestEqual(TEXT("CONTROL: without the option the imprint is done"), static_cast<int32>(ControlResult),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestTrue(TEXT("CONTROL: with the plain draw's rolls"),
			 ControlDone.Drawn.PositiveRoll == Plain.PositiveRoll && ControlDone.Drawn.NegativeRoll == Plain.NegativeRoll);
	TestTrue(TEXT("CONTROL: and its days"), ControlDone.Days > 0);
	TestEqual(TEXT("both Shards are used"), Scene.Bag()->CountOfMaterial(Shard), 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M4. The Master Tinkerer's change of drawback: the positive kept, the milder of two negative rolls, no days.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Slice F3's Legendary helm, More Health with No Armour, its drawback changed, on a kill count whose critical draw is
 * below 0.05 and whose second negative is lower than the plain draw's: the positive and its 0.5 kept, the plain draw's
 * negative at the second (lower) roll, no day passes, residue 200 to 300. Prints "TINKERER CHANGE".
 */
CATACLYSM_MILESTONES_TEST(FCataclysmForgeMilestonesChangeTest,
	"Cataclysm.ForgeMilestones.TheMasterTinkerersChangeOfDrawbackKeepsTheMilderRoll")
{
	using namespace CataclysmForgeMilestonesTest;
	FMilestonesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()) || !TheTables(*this))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const int32 Mask = Scene.Run->WalkSeedMask;
	const int32 Day = Scene.Run->Day();
	FCataclysmRolledEnchantment Plain;
	float Critical = 1.0f;
	float SecondPositive = 0.0f;
	float SecondNegative = 1.0f;
	const int32 Kills = KillsWhere([&](int32 Each)
	{
		const FCataclysmItem Helm = LegendaryHelm(Each);
		if (!UCataclysmForge::DrawChangedNegative(Helm, 0, UCataclysmItemModifiers::LoadBaseTable(),
												  UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
												  UCataclysmDropRoll::LoadNegativeEnchantmentTable(), Mask, Day, Plain))
		{
			return false;
		}
		CriticalDraws(Helm, 0, Mask, Day, Critical, SecondPositive, SecondNegative);
		return Critical < 0.05f && SecondNegative < Plain.NegativeRoll;
	});
	if (!TestTrue(TEXT("set-up: a helm whose draw is a critical success that lowers the negative was found"),
				  Kills != INDEX_NONE))
	{
		return false;
	}

	Scene.Hold(ImperialVanguard, MasterTinkererOption);
	const int32 Cell = Scene.Bag()->AddItem(LegendaryHelm(Kills));
	Scene.Bag()->AddMaterial(Essence, 1);
	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::ChangeNegative(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	AddInfo(FString::Printf(TEXT("TINKERER CHANGE: kills %d, critical %.4f; plain %s %.4f, second %.4f; now %s %.4f, "
								 "positive %.4f; %d days, residue %.1f '%s'"),
							Kills, Critical, *Plain.Negative.ToString(), Plain.NegativeRoll, SecondNegative,
							After && After->Enchantments.Num() == 1 ? *After->Enchantments[0].Negative.ToString() : TEXT("-"),
							After && After->Enchantments.Num() == 1 ? After->Enchantments[0].NegativeRoll : -1.0f,
							After && After->Enchantments.Num() == 1 ? After->Enchantments[0].PositiveRoll : -1.0f,
							Scene.Run->Day() - Day, After ? After->Residue : -1.0f, *Message));

	TestEqual(TEXT("the change is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestTrue(TEXT("the helm holds one pair"), After != nullptr && After->Enchantments.Num() == 1))
	{
		return false;
	}
	const FCataclysmRolledEnchantment& Now = After->Enchantments[0];
	TestTrue(TEXT("the positive and its roll kept (R1)"), Now.Positive == MoreHealth && Now.PositiveRoll == 0.5f);
	TestTrue(TEXT("the plain draw's drawback"), Now.Negative == Plain.Negative);
	TestEqual(TEXT("**at the lower of two rolls**"), Now.NegativeRoll, SecondNegative, 0.000001f);
	TestEqual(TEXT("**no day passes**"), Scene.Run->Day(), Day);
	TestEqual(TEXT("the residue is still added: 200 to 300"), After->Residue, 300.0f, 0.001f);
	TestEqual(TEXT("the Regret Essence is used"), Scene.Bag()->CountOfMaterial(Essence), 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M5. Both options can be chosen, give their chances, and Imperial Prowess can take one as its extra pick.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Neither option's row waits on anything; The Soul Forge held gives 0.15 and nothing else, The Master Tinkerer 0.05;
 * CONTROL: The Collector's Decree, Edicts of Power's second, gives neither. With Foundations' The Delver, Imperial
 * Prowess and Edicts of Power's The Sentinel taken, The Soul Forge is open as the extra option, is taken, and its chance
 * is held beside The Sentinel. Prints "MILESTONE OPTIONS".
 */
CATACLYSM_MILESTONES_TEST(FCataclysmForgeMilestonesOptionsTest,
	"Cataclysm.ForgeMilestones.BothOptionsCanBeChosenAndImperialProwessCanTakeEither")
{
	using namespace CataclysmForgeMilestonesTest;
	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	const FCataclysmEmpireTreeNodeRow* Edicts =
		Nodes->FindRow<FCataclysmEmpireTreeNodeRow>(EdictsOfPower, TEXT("ForgeMilestones"), false);
	const FCataclysmEmpireTreeNodeRow* Vanguard =
		Nodes->FindRow<FCataclysmEmpireTreeNodeRow>(ImperialVanguard, TEXT("ForgeMilestones"), false);
	if (!TestTrue(TEXT("set-up: both milestones' rows"), Edicts && Vanguard))
	{
		return false;
	}
	FCataclysmPassiveAllocation SoulForge;
	Take(SoulForge, EdictsOfPower, SoulForgeOption);
	FCataclysmPassiveAllocation Tinkerer;
	Take(Tinkerer, ImperialVanguard, MasterTinkererOption);
	FCataclysmPassiveAllocation Decree;
	Take(Decree, EdictsOfPower, 2);
	const FCataclysmEmpireTreePlayerBonuses FromSoulForge = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, SoulForge);
	const FCataclysmEmpireTreePlayerBonuses FromTinkerer = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tinkerer);
	const FCataclysmEmpireTreePlayerBonuses FromDecree = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Decree);

	FCataclysmPassiveAllocation Prowess;
	Take(Prowess, Foundations, 2);
	Take(Prowess, ImperialZenith, 1);
	Take(Prowess, EdictsOfPower, 1);
	const FString Open = UCataclysmEmpireTree::RefusalForChoosingExtraOption(Nodes, Effects, Prowess, EdictsOfPower,
																			 SoulForgeOption);
	FString Reason;
	const bool bTaken =
		UCataclysmEmpireTree::ChooseExtraOption(Nodes, Effects, Prowess, EdictsOfPower, SoulForgeOption, Reason);
	const FCataclysmEmpireTreePlayerBonuses FromExtra = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Prowess);
	AddInfo(FString::Printf(TEXT("MILESTONE OPTIONS: waits [%s] [%s]; The Soul Forge %.2f / %.2f; The Master Tinkerer "
								 "%.2f / %.2f; The Collector's Decree %.2f / %.2f; extra [%s] taken %d [%s], %.2f"),
							*Edicts->Option3WaitsOn, *Vanguard->Option2WaitsOn, FromSoulForge.UpgradeExtraLevelChance,
							FromSoulForge.EnchantCriticalChance, FromTinkerer.UpgradeExtraLevelChance,
							FromTinkerer.EnchantCriticalChance, FromDecree.UpgradeExtraLevelChance,
							FromDecree.EnchantCriticalChance, *Open, bTaken ? 1 : 0, *Reason,
							FromExtra.UpgradeExtraLevelChance));

	TestTrue(TEXT("The Soul Forge waits on nothing"), Edicts->Option3WaitsOn.IsEmpty());
	TestTrue(TEXT("nor The Master Tinkerer"), Vanguard->Option2WaitsOn.IsEmpty());
	TestEqual(TEXT("**The Soul Forge gives 15% to an upgrade**"), FromSoulForge.UpgradeExtraLevelChance, 0.15f, 0.0001f);
	TestEqual(TEXT("and nothing to an enchantment"), FromSoulForge.EnchantCriticalChance, 0.0f);
	TestEqual(TEXT("**The Master Tinkerer gives 5% to an enchantment**"), FromTinkerer.EnchantCriticalChance, 0.05f,
			  0.0001f);
	TestEqual(TEXT("and nothing to an upgrade"), FromTinkerer.UpgradeExtraLevelChance, 0.0f);
	TestTrue(TEXT("CONTROL: The Collector's Decree gives neither"),
			 FromDecree.UpgradeExtraLevelChance == 0.0f && FromDecree.EnchantCriticalChance == 0.0f);
	TestTrue(TEXT("**Imperial Prowess may take The Soul Forge**"), Open.IsEmpty());
	TestTrue(TEXT("and takes it"), bTaken && Reason.IsEmpty());
	TestEqual(TEXT("whose chance is held beside The Sentinel"), FromExtra.UpgradeExtraLevelChance, 0.15f, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// M6. The first press says the chance, and the play log says what happened.
// --------------------------------------------------------------------------------------------------------------------
/** The two chance sentences, none for nought, and the two play log lines, word for word. Prints "MILESTONE WORDS". */
CATACLYSM_MILESTONES_TEST(FCataclysmForgeMilestonesWordsTest,
	"Cataclysm.ForgeMilestones.TheForgeSaysTheChanceAndTheLineSaysWhatHappened")
{
	const FString Upgrade = UCataclysmForge::MilestoneChanceText(0.15f, /*bUpgrade=*/true);
	const FString Enchant = UCataclysmForge::MilestoneChanceText(0.05f, /*bUpgrade=*/false);
	const FString None = UCataclysmForge::MilestoneChanceText(0.0f, /*bUpgrade=*/true);
	const FString SoulForgeLine = FCataclysmPlayLoopLog::ForgeSoulForgeLine(TEXT("Quality Helm"), 3, 5);
	const FString CriticalLine = FCataclysmPlayLoopLog::ForgeCriticalLine(TEXT("Quality Helm"));
	AddInfo(FString::Printf(TEXT("MILESTONE WORDS: \"%s\" / \"%s\" / \"%s\" / \"%s\" / \"%s\""), *Upgrade, *Enchant, *None,
							*SoulForgeLine, *CriticalLine));

	TestEqual(TEXT("**an upgrade's first press, with The Soul Forge**"), Upgrade,
			  FString(TEXT(" The Soul Forge: a 15% chance it rises two levels instead of one.")));
	TestEqual(TEXT("**an enchantment craft's, with The Master Tinkerer**"), Enchant,
			  FString(TEXT(" The Master Tinkerer: a 5% chance of a critical success, its rolls drawn twice with the "
						   "better kept, taking no days.")));
	TestTrue(TEXT("nothing without the option"), None.IsEmpty());
	TestEqual(TEXT("**the Soul Forge's line**"), SoulForgeLine,
			  FString(TEXT("The Soul Forge raised Quality Helm two levels, +3 to +5")));
	TestEqual(TEXT("**the critical success's line**"), CriticalLine,
			  FString(TEXT("a critical success on Quality Helm: the rolls were drawn twice with the better kept, and the "
						   "craft took no days")));
	return true;
}

#undef CATACLYSM_MILESTONES_TEST

#endif // WITH_AUTOMATION_TESTS
