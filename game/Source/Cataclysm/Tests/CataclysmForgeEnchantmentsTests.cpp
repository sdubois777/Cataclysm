// Copyright Stephen Dubois. All Rights Reserved.

/**
 * THE FORGE'S IMPRINT ENCHANTMENT AND CHANGE ENCHANTMENT NEGATIVE. Issue #47, slice F3, ruled by the coordinating session
 * on 2026-11-22 (the forge survey's Q14 to Q18).
 *
 * The Crafting sheet's rows: Imprint Enchantment, an Aetherial Shard, 5 residue, 2 base days, Base Gold 500; Change
 * Enchantment Negative, a Regret Essence, 100 residue, 7 base days, Base Gold 1,750; the residue's time penalty and the
 * gold formula on both. An imprint draws a pair as a drop draws them and gives up the affix the player chooses, only on
 * a piece with every slot filled at the gear level its new rarity needs; a change draws a different negative at or below
 * the kept positive's weight, with a new roll, and never touches a set's pair.
 *
 * AN IMPRINT CAN DRAW A SET (FE12, ruled 2026-11-22, F3-Q2), and the piece counts toward its set's bonus as a dropped
 * one does.
 *
 * AND G10'S TWO GEM ENCHANTMENTS, END TO END (FE8 to FE10): an imprint that draws "Sockets on this equipment fit any gem
 * type" lets an off-type gem in at the forge, and a change that takes "Gems in this equipment are 50% less effective"
 * away or draws it on moves the gem's value. No F3 craft removes a positive, so the piece LOSING fit-any (G10's Q2) is
 * still reached only by `Cataclysm.GemEnchantments.AGemTheEnchantmentLetInStopsActingWhenItIsReplaced`.
 *
 * EVERY TEST READS THE TABLES THE GAME LOADS. The scene is the one `CataclysmForgeSocketsTests.cpp` builds, copied into
 * this file's namespace because the module is built in unity files.
 */

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

namespace CataclysmForgeEnchantmentsTest
{
	const FName Shard(UCataclysmForge::AetherialShardRow);
	const FName Essence(UCataclysmForge::RegretEssenceRow);
	const FName Agent(UCataclysmForge::SettingAgentRow);

	/** G10's two rows, and rows the tests pair them with, as `tools/generate_datatables.py` names them. */
	const FName FitsAny(TEXT("Positive_Sockets_on_this_equipment_fit_any_gem_type"));				// weight 3, weapons
	const FName LessEffective(TEXT("Negative_Gems_in_this_equipment_are_50_less_effective"));		// weight 3
	const FName MoreHealth(TEXT("Positive_Gain_10_30_increased_health"));							// weight 3
	const FName DoubleShield(TEXT("Positive_Double_your_energy_shield"));							// weight 1
	const FName DamageTypes(TEXT("Positive_This_weapon_has_2_4_damage_types"));						// weight 1, weapons
	const FName ArchonsAegis(TEXT("Positive_Archon_s_Aegis_2_Piece_Bonus_Your_block_chanc"));		// a set's row
	const FName NoArmour(TEXT("Negative_You_have_no_armor"));										// weight 1
	const FName HealthReserved(TEXT("Negative_80_99_of_your_health_is_reserved"));					// weight 1

	const FName OfLife(TEXT("Gem_Of_Life"));	// Defense; max_health increased, 25 at Superb

	FCataclysmRolledAffix Affix(const TCHAR* Row, float Roll = 0.5f)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Row);
		Out.Tier = 2;
		Out.Roll = Roll;
		return Out;
	}

	FCataclysmRolledEnchantment Pair(FName Positive, FName Negative)
	{
		FCataclysmRolledEnchantment Out;
		Out.Positive = Positive;
		Out.Negative = Negative;
		Out.PositiveRoll = 0.5f;
		Out.NegativeRoll = 0.5f;
		return Out;
	}

	FCataclysmSocket Socket(ECataclysmSocketType Type, FName Gem = NAME_None)
	{
		FCataclysmSocket Out;
		Out.Type = Type;
		Out.Gem = Gem;
		Out.GemRarity = Gem.IsNone() ? ECataclysmRarity::Everyday : ECataclysmRarity::Superb;
		return Out;
	}

	/** A Masterful sword at gear level `Level`: four affixes, one empty Attack socket, residue 150. */
	FCataclysmItem MasterfulSword(int32 Level = 4)
	{
		FCataclysmItem Out;
		Out.Base = FName(TEXT("Weapon_Sword"));
		Out.GearLevel = Level;
		Out.Residue = 150.0f;
		Out.DamageTypes = {FName(TEXT("War"))};
		Out.Affixes = {Affix(TEXT("Stat_Flat_damage")), Affix(TEXT("Stat_Increased_damage")),
					   Affix(TEXT("Stat_Flat_critical_strike_chance")), Affix(TEXT("Stat_Increased_attack_speed"))};
		Out.Sockets = {Socket(ECataclysmSocketType::Attack)};
		return Out;
	}

	/** A Legendary helm at gear level 6: three affixes and the pair given, Of Life in its Defense socket, residue 200. */
	FCataclysmItem LegendaryHelm(const FCataclysmRolledEnchantment& Held)
	{
		FCataclysmItem Out;
		Out.Base = FName(TEXT("Head_Helm"));
		Out.GearLevel = 6;
		Out.Residue = 200.0f;
		Out.Affixes = {Affix(TEXT("Stat_Flat_maximum_health")), Affix(TEXT("Stat_Flat_armor")),
					   Affix(TEXT("Stat_Flat_block_chance"))};
		Out.Enchantments = {Held};
		Out.EnchantmentCount = 1;
		Out.Sockets = {Socket(ECataclysmSocketType::Defense, OfLife)};
		return Out;
	}

	/** A row's sheet weight, or 0 when the table does not hold it. */
	float WeightOf(const UDataTable* Table, FName Row)
	{
		const FCataclysmEnchantmentRow* Found = Table != nullptr
			? Table->FindRow<FCataclysmEnchantmentRow>(Row, TEXT("CataclysmForgeEnchantmentsTest"), false)
			: nullptr;
		return Found != nullptr ? Found->Weight : 0.0f;
	}

	/** The value of the one gem modifier on `Stat` among the piece's modifiers, or -1 when there is none or several. */
	float GemValueOn(const FCataclysmItem& Item, const TCHAR* Stat)
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Totals = UCataclysmItemModifiers::ModifiersFor(
			Item, UCataclysmItemModifiers::LoadBaseTable(), UCataclysmDropRoll::LoadAffixTable());
		TArray<float> Found;
		if (const TArray<FCataclysmStatModifier>* OnStat = Totals.Find(FName(Stat)))
		{
			for (const FCataclysmStatModifier& Modifier : *OnStat)
			{
				if (Modifier.Source == ECataclysmModifierSource::Gem)
				{
					Found.Add(Modifier.Value);
				}
			}
		}
		return Found.Num() == 1 ? Found[0] : -1.0f;
	}

	/** The tables the game loads. FALSE, SAYING SO, so a missing table fails the set-up and not an assertion. */
	bool TheTables(FAutomationTestBase& Test)
	{
		return Test.TestTrue(TEXT("set-up: the generated base, affix, gear rarity, enchantment, effect, gem and weapon "
								  "skill tables"),
							 UCataclysmItemModifiers::LoadBaseTable() && UCataclysmDropRoll::LoadAffixTable()
								 && UCataclysmDropRoll::LoadGearRarityTable()
								 && UCataclysmDropRoll::LoadPositiveEnchantmentTable()
								 && UCataclysmDropRoll::LoadNegativeEnchantmentTable()
								 && UCataclysmItemModifiers::LoadEnchantmentEffectTable()
								 && UCataclysmGems::LoadGeneratedTable() && UCataclysmWeaponSkills::LoadGeneratedTable());
	}

	struct FForgeEnchantmentsScene
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
	FForgeEnchantmentsScene MakeScene()
	{
		FForgeEnchantmentsScene Out;
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
			// SOLO SELF-FOUND, WITH GOLD OF ITS OWN, so every craft can pay (#2830) and no account record is written.
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Imprinter"), Partition);
			Out.State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Out.Run->Begin(/* InSeed */ 4747, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
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
// FE1. Imprinting: the rule's figures, its question and every refusal.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsImprintRuleTest,
	"Cataclysm.ForgeEnchantments.ImprintingTakesAnAetherialShardFiveResidueTwoDaysAndAFullPieceAtItsGearLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Masterful sword at +4 with residue 150 may give up its second affix for an imprint: an Aetherial Shard, residue 155,
 * 2 days and 1 more for 150 residue, 500 gold times (150 / 50 + 1) = 2,000, and it becomes Legendary, which needs +4.
 * REFUSED: a Superb piece (a slot empty), a Cataclysmic one (no affix left), affix 8, the same sword at +3, a Legendary
 * at +5 (Mythical needs +6), no shard, too little gold, no piece, and no gear rarity table.
 */
bool FCataclysmForgeEnchantmentsImprintRuleTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const FCataclysmItem Sword = MasterfulSword();

	const FCataclysmForgeEnchantmentWork Plan = UCataclysmForge::ImprintOf(Sword, 1, 1, Rarities, 1, 1000000);
	TestTrue(TEXT("a Masterful sword at +4 can be imprinted"), Plan.bPossible);
	TestTrue(TEXT("it is an imprint"), Plan.bImprint);
	TestEqual(TEXT("giving up its second affix"), Plan.AffixRow, FName(TEXT("Stat_Increased_damage")));
	TestEqual(TEXT("with an Aetherial Shard"), Plan.Material, Shard);
	TestEqual(TEXT("residue 150 becomes 155"), Plan.ResidueAfter, 155.0f, 0.001f);
	TestEqual(TEXT("2 days and 1 for 150 residue"), Plan.Days, 3);
	TestEqual(TEXT("500 gold a time, at 150 residue 2,000"), Plan.Gold, static_cast<int64>(2000));
	TestTrue(TEXT("Masterful before"), Plan.RarityBefore == ECataclysmRarity::Masterful);
	TestTrue(TEXT("Legendary after"), Plan.RarityAfter == ECataclysmRarity::Legendary);
	TestEqual(TEXT("which needs gear level 4"), Plan.GearLevelNeeded, 4);
	TestTrue(TEXT("nothing is drawn in a plan"), Plan.Drawn.Positive.IsNone());
	TestEqual(TEXT("the first press's question"), UCataclysmForge::EnchantmentWorkQuestion(Plan, TEXT("affix 2")),
			  FString(TEXT("Press again to imprint an enchantment, giving up affix 2: one Aetherial Shard, 2,000 gold, "
						   "residue 150 to 155, 3 days of the run; the piece becomes Legendary and the enchantment is "
						   "drawn at random.")));

	FCataclysmItem Superb = Sword;
	Superb.Affixes.SetNum(3);
	TestEqual(TEXT("a Superb piece has a slot empty"), UCataclysmForge::ImprintOf(Superb, 0, 1, Rarities, 1, 1000000).Refusal,
			  FString(TEXT("Only a piece with every slot filled, Masterful or better, takes an imprint.")));
	FCataclysmItem Cataclysmic = Sword;
	Cataclysmic.GearLevel = 10;
	Cataclysmic.Affixes.Reset();
	Cataclysmic.Enchantments = {Pair(MoreHealth, NoArmour), Pair(FitsAny, LessEffective), Pair(DoubleShield, HealthReserved),
								Pair(DamageTypes, FName(TEXT("Negative_You_lose_15_of_your_max_hp_every_5_seconds")))};
	Cataclysmic.EnchantmentCount = 4;
	TestEqual(TEXT("a Cataclysmic piece has no affix to give up"),
			  UCataclysmForge::ImprintOf(Cataclysmic, 0, 1, Rarities, 1, 1000000).Refusal,
			  FString(TEXT("That piece holds four enchantments; it has no affix left to give up.")));
	TestEqual(TEXT("no affix 8"), UCataclysmForge::ImprintOf(Sword, 7, 1, Rarities, 1, 1000000).Refusal,
			  FString(TEXT("That piece has no affix 8.")));
	TestEqual(TEXT("a Masterful sword at +3 cannot become Legendary"),
			  UCataclysmForge::ImprintOf(MasterfulSword(3), 0, 1, Rarities, 1, 1000000).Refusal,
			  FString(TEXT("A Legendary piece needs gear level 4, and that piece is +3. Upgrade it first.")));
	FCataclysmItem Legendary = MasterfulSword(5);
	Legendary.Affixes.SetNum(3);
	Legendary.Enchantments = {Pair(MoreHealth, NoArmour)};
	Legendary.EnchantmentCount = 1;
	const FCataclysmForgeEnchantmentWork ToMythical = UCataclysmForge::ImprintOf(Legendary, 0, 1, Rarities, 1, 1000000);
	TestEqual(TEXT("a Legendary at +5 cannot become Mythical"), ToMythical.Refusal,
			  FString(TEXT("A Mythical piece needs gear level 6, and that piece is +5. Upgrade it first.")));
	Legendary.GearLevel = 6;
	TestTrue(TEXT("and at +6 it can"), UCataclysmForge::ImprintOf(Legendary, 0, 1, Rarities, 1, 1000000).bPossible);
	TestEqual(TEXT("no Aetherial Shard"), UCataclysmForge::ImprintOf(Sword, 1, 0, Rarities, 1, 1000000).Refusal,
			  FString(TEXT("Imprinting an enchantment takes an Aetherial Shard; 0 held.")));
	TestEqual(TEXT("too little gold"), UCataclysmForge::ImprintOf(Sword, 1, 1, Rarities, 1, 100).Refusal,
			  FString(TEXT("Imprinting an enchantment takes 2,000 gold; 100 gold held.")));
	TestEqual(TEXT("no piece"), UCataclysmForge::ImprintOf(FCataclysmItem(), 1, 1, Rarities, 1, 1000000).Refusal,
			  FString(TEXT("There is nothing there to imprint.")));
	TestEqual(TEXT("no gear rarity table: refused, never read as no gate"),
			  UCataclysmForge::ImprintOf(Sword, 1, 1, nullptr, 1, 1000000).Refusal,
			  FString(TEXT("The gear rarity table could not be read, so the gear level an imprint needs is not known.")));
	AddInfo(FString::Printf(TEXT("IMPRINT RULE: %d days, %lld gold, residue %.0f to %.0f; then '%s'"), Plan.Days,
							Plan.Gold, Plan.ResidueBefore, Plan.ResidueAfter, *ToMythical.Refusal));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE2. The imprint's draw: the pairing floor, nothing repeated from the piece, the same draw for the same day.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsImprintDrawTest,
	"Cataclysm.ForgeEnchantments.AnImprintDrawsAPairAsADropDoesAndRepeatsNothingOnThePiece",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Legendary sword at +6 holding "Gain 10-30% increased health" with "You have no armor", drawn on days 1 to 400 of one
 * run: every pair's negative weighs no more than its positive (the owner's floor, "A NEGATIVE IS NEVER MILDER THAN THE
 * POSITIVE IT IS PAIRED WITH"); no pair repeats either half the sword holds; every positive is one a weapon may carry;
 * the draw is the same when asked twice for one day. THE CONTROL: the draws differ across the days, so a draw that
 * ignored the day would fail. And `RollEnchantments` itself, with every row a weapon can draw held but one of each
 * kind in each band, draws only those left. Prints "IMPRINT DRAWS" and "TAKEN".
 */
bool FCataclysmForgeEnchantmentsImprintDrawTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Positives = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const UDataTable* Negatives = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	FCataclysmItem Sword = MasterfulSword(6);
	Sword.Affixes.SetNum(3);
	Sword.Enchantments = {Pair(MoreHealth, NoArmour)};
	Sword.EnchantmentCount = 1;
	constexpr int32 Mask = 12345;

	int32 Drawn = 0;
	int32 Milder = 0;
	int32 Repeated = 0;
	int32 NotForAWeapon = 0;
	int32 Unstable = 0;
	TSet<FName> DistinctPositives;
	int32 ByWeight[5] = {0, 0, 0, 0, 0};
	for (int32 Day = 1; Day <= 400; ++Day)
	{
		FCataclysmRolledEnchantment First;
		FCataclysmRolledEnchantment Again;
		FRandomStream Unused;
		if (!UCataclysmForge::DrawImprint(Sword, Bases, Positives, Negatives, Mask, Day, First, Unused)
			|| !UCataclysmForge::DrawImprint(Sword, Bases, Positives, Negatives, Mask, Day, Again, Unused))
		{
			continue;
		}
		++Drawn;
		Unstable += (First.Positive != Again.Positive || First.Negative != Again.Negative
					 || First.PositiveRoll != Again.PositiveRoll || First.NegativeRoll != Again.NegativeRoll) ? 1 : 0;
		const float PositiveWeight = WeightOf(Positives, First.Positive);
		const float NegativeWeight = WeightOf(Negatives, First.Negative);
		const FCataclysmEnchantmentRow* Row =
			Positives->FindRow<FCataclysmEnchantmentRow>(First.Positive, TEXT("FE2"), false);
		const bool bSet = Row != nullptr && UCataclysmDropRoll::EnchantmentSetId(*Row) > 0;
		// A SET BRINGS ITS OWN NEGATIVE, "paired and guaranteed", so the floor is the ordinary pairs'.
		Milder += (!bSet && NegativeWeight > PositiveWeight) ? 1 : 0;
		Repeated += (First.Positive == MoreHealth || First.Negative == NoArmour) ? 1 : 0;
		NotForAWeapon += (Row == nullptr || !UCataclysmDropRoll::EnchantmentSuitsSlot(*Row, TEXT("Weapon"))) ? 1 : 0;
		DistinctPositives.Add(First.Positive);
		ByWeight[FMath::Clamp(FMath::RoundToInt(PositiveWeight), 0, 4)] += 1;
	}
	AddInfo(FString::Printf(TEXT("IMPRINT DRAWS: %d of 400 drawn, %d distinct positives; by positive weight 1:%d 2:%d 3:%d "
								 "4:%d; milder %d, repeated %d, not for a weapon %d, unstable %d"),
							Drawn, DistinctPositives.Num(), ByWeight[1], ByWeight[2], ByWeight[3], ByWeight[4], Milder,
							Repeated, NotForAWeapon, Unstable));
	TestEqual(TEXT("every day draws a pair"), Drawn, 400);
	TestEqual(TEXT("no negative is milder than its positive"), Milder, 0);
	TestEqual(TEXT("no half the sword holds is drawn again"), Repeated, 0);
	TestEqual(TEXT("every positive may sit on a weapon"), NotForAWeapon, 0);
	TestEqual(TEXT("the same day draws the same pair and rolls"), Unstable, 0);
	TestTrue(TEXT("control: the days draw different pairs"), DistinctPositives.Num() > 50);
	TestTrue(TEXT("weight 4 positives are the commonest, as a drop's are"), ByWeight[4] > ByWeight[3]
		&& ByWeight[3] > ByWeight[2]);

	// THE TAKEN ROWS ARE NEVER DRAWN, asked of `RollEnchantments` itself: with every row a weapon can draw on the list
	// of pairs already held but the first positive and the first negative of each weight band, and every set's row too,
	// each of 50 draws gives a positive and a negative from those left. One row is left in every band so that no band
	// is empty, which the draw logs as a fault in the pool. A draw that ignored the list would give others almost every
	// time: the rows left are 4 of the 334 positives and 4 of the 181 negatives a weapon can draw (2026-11-22).
	TArray<TArray<FName>> PositivesByWeight;
	TArray<TArray<FName>> NegativesByWeight;
	UCataclysmDropRoll::EnchantmentCandidatesByWeight(Positives, TEXT("Weapon"), PositivesByWeight);
	UCataclysmDropRoll::EnchantmentCandidatesByWeight(Negatives, TEXT("Weapon"), NegativesByWeight);
	TArray<FCataclysmEnchantmentSet> Sets;
	UCataclysmDropRoll::EnchantmentSetsFor(Positives, Negatives, TEXT("Weapon"), Sets);
	TSet<FName> LeftPositives;
	TSet<FName> LeftNegatives;
	TArray<FCataclysmRolledEnchantment> Held;
	for (int32 Band = 0; Band < PositivesByWeight.Num() && Band < NegativesByWeight.Num(); ++Band)
	{
		for (int32 Index = 0; Index < PositivesByWeight[Band].Num(); ++Index)
		{
			if (Index == 0)
			{
				LeftPositives.Add(PositivesByWeight[Band][Index]);
			}
			else
			{
				Held.Add(Pair(PositivesByWeight[Band][Index], NAME_None));
			}
		}
		for (int32 Index = 0; Index < NegativesByWeight[Band].Num(); ++Index)
		{
			if (Index == 0)
			{
				LeftNegatives.Add(NegativesByWeight[Band][Index]);
			}
			else
			{
				Held.Add(Pair(NAME_None, NegativesByWeight[Band][Index]));
			}
		}
	}
	for (const FCataclysmEnchantmentSet& Set : Sets)
	{
		Held.Add(Pair(Set.Representative, NAME_None));
	}
	if (!TestTrue(TEXT("set-up: one positive and one negative left in each of the four bands"),
				  LeftPositives.Num() == 4 && LeftNegatives.Num() == 4))
	{
		return false;
	}
	int32 OnlyTheLeft = 0;
	for (int32 Seed = 1; Seed <= 50; ++Seed)
	{
		FRandomStream Stream(Seed);
		TArray<FCataclysmRolledEnchantment> Rolled;
		OnlyTheLeft += UCataclysmDropRoll::RollEnchantments(Positives, Negatives, TEXT("Weapon"), 1, Stream, Rolled, &Held)
				&& Rolled.Num() == 1 && LeftPositives.Contains(Rolled[0].Positive)
				&& LeftNegatives.Contains(Rolled[0].Negative)
			? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("TAKEN: %d rows held; %d of 50 draws gave only rows left"), Held.Num(), OnlyTheLeft));
	TestEqual(TEXT("the taken rows are never drawn: every draw gives rows left"), OnlyTheLeft, 50);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE3. Applying an imprint: the chosen affix goes, the pair comes, and a damage types row tops up the weapon's types.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsApplyTest,
	"Cataclysm.ForgeEnchantments.AnImprintGivesUpTheChosenAffixAndTopsUpAWeaponsDamageTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Masterful sword gives up its third affix for "Gain 10-30% increased health": three affixes left in their order,
 * one pair, a count of 1 agreeing with it, Legendary. "This weapon has 2-4 damage types" at the top of its range tops
 * the sword's one damage type up to what the row and the sword allow, as a drop does, keeping the type it had. THE
 * CONTROL: the health pair leaves the damage types as they were. Prints "APPLIED".
 */
bool FCataclysmForgeEnchantmentsApplyTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Effects = UCataclysmItemModifiers::LoadEnchantmentEffectTable();
	const FCataclysmItem Sword = MasterfulSword();
	FRandomStream Stream(47);

	FCataclysmItem Health;
	if (!TestTrue(TEXT("the health pair applies"),
				  UCataclysmForge::ApplyImprint(Sword, 2, Pair(MoreHealth, NoArmour), Bases, Skills, Effects, Stream,
												Health)))
	{
		return false;
	}
	TestEqual(TEXT("three affixes left"), Health.Affixes.Num(), 3);
	TestTrue(TEXT("the third, Stat_Flat_critical_strike_chance, is the one gone, the others in their order"),
			 Health.Affixes.Num() == 3 && Health.Affixes[0].Affix == Sword.Affixes[0].Affix
				 && Health.Affixes[1].Affix == Sword.Affixes[1].Affix && Health.Affixes[2].Affix == Sword.Affixes[3].Affix);
	TestEqual(TEXT("one pair"), Health.Enchantments.Num(), 1);
	TestEqual(TEXT("and a count agreeing with it"), Health.EnchantmentCount, 1);
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	TestTrue(TEXT("a Legendary"), UCataclysmItemModifiers::RarityOfItem(Health, Rarity)
								 && Rarity == ECataclysmRarity::Legendary);
	TestEqual(TEXT("control: the health pair adds no damage type"), Health.DamageTypes.Num(), 1);

	FCataclysmRolledEnchantment Types = Pair(DamageTypes, NoArmour);
	Types.PositiveRoll = 1.0f;
	FCataclysmItem Typed;
	TestTrue(TEXT("the damage types pair applies"),
			 UCataclysmForge::ApplyImprint(Sword, 0, Types, Bases, Skills, Effects, Stream, Typed));
	const int32 Carried = UCataclysmDropRoll::DamageTypesCarriedBy({Types}, Effects);
	const FCataclysmItemBaseRow* SwordRow =
		Bases->FindRow<FCataclysmItemBaseRow>(Sword.Base, TEXT("FE3"), false);
	const int32 Ceiling = SwordRow != nullptr
		? UCataclysmDropRoll::MaxDamageTypesFor(
			  *SwordRow, UCataclysmDropRoll::DamageTypesAvailableTo(Skills, SwordRow->WeaponType).Num())
		: 0;
	AddInfo(FString::Printf(TEXT("APPLIED: health %d affixes, %d pair; damage types row carries %d, sword ceiling %d, "
								 "types %d"),
							Health.Affixes.Num(), Health.Enchantments.Num(), Carried, Ceiling, Typed.DamageTypes.Num()));
	TestTrue(TEXT("set-up: the row carries more types than the sword holds"), Carried > 1 && Ceiling > 1);
	TestEqual(TEXT("the sword carries what the row and the sword allow"), Typed.DamageTypes.Num(),
			  FMath::Min(Carried, Ceiling));
	TestTrue(TEXT("keeping the type it had"), Typed.DamageTypes.Contains(FName(TEXT("War"))));

	FCataclysmItem Nothing;
	TestFalse(TEXT("no affix 5: not applied"),
			  UCataclysmForge::ApplyImprint(Sword, 4, Pair(MoreHealth, NoArmour), Bases, Skills, Effects, Stream, Nothing));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE4. The imprint craft on a carried Masterful sword.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsImprintCraftTest,
	"Cataclysm.ForgeEnchantments.TheImprintCraftMakesAMasterfulSwordLegendaryWithThePairItsDayDraws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The sword gives up its second affix: the shard is used, 2,000 gold paid, 3 days pass, residue 155, the affix is gone
 * and the pair is the one `UCataclysmForge::DrawImprint` gives for the run's mask and that day, so a reload draws the
 * same. A second imprint, to Mythical, is refused at +4 and nothing moves. Prints "IMPRINTED".
 */
bool FCataclysmForgeEnchantmentsImprintCraftTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FForgeEnchantmentsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmItem Sword = MasterfulSword();
	const int32 Cell = Scene.Bag()->AddItem(Sword);
	Scene.Bag()->AddMaterial(Shard, 2);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	FCataclysmRolledEnchantment Expected;
	FRandomStream Unused;
	TestTrue(TEXT("set-up: the day's draw is known"),
			 UCataclysmForge::DrawImprint(Sword, UCataclysmItemModifiers::LoadBaseTable(),
										  UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
										  UCataclysmDropRoll::LoadNegativeEnchantmentTable(), Scene.Run->WalkSeedMask,
										  DayBefore, Expected, Unused));

	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Imprint(Scene.World, FCataclysmForgeTarget::Carried(Cell), 1, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the imprint is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the sword is still in its cell"), After)
		|| !TestEqual(TEXT("it holds one pair"), After->Enchantments.Num(), 1))
	{
		AddError(Message);
		return false;
	}
	TestEqual(TEXT("and three affixes"), After->Affixes.Num(), 3);
	TestFalse(TEXT("its second, Stat_Increased_damage, gone"), After->Affixes.ContainsByPredicate(
		[](const FCataclysmRolledAffix& Each) { return Each.Affix == FName(TEXT("Stat_Increased_damage")); }));
	TestTrue(TEXT("the pair the craft reports"), After->Enchantments[0].Positive == Done.Drawn.Positive
											   && After->Enchantments[0].Negative == Done.Drawn.Negative);
	TestTrue(TEXT("is the pair the day draws, so a reload draws the same"),
			 Done.Drawn.Positive == Expected.Positive && Done.Drawn.Negative == Expected.Negative
				 && Done.Drawn.PositiveRoll == Expected.PositiveRoll && Done.Drawn.NegativeRoll == Expected.NegativeRoll);
	TestEqual(TEXT("residue 150 becomes 155"), After->Residue, 155.0f, 0.001f);
	TestEqual(TEXT("one Aetherial Shard is used"), Scene.Bag()->CountOfMaterial(Shard), 1);
	TestEqual(TEXT("three days passed"), Scene.Run->Day(), DayBefore + 3);
	TestEqual(TEXT("2,000 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(2000));

	// A SECOND IMPRINT WOULD MAKE IT MYTHICAL, WHICH NEEDS +6: refused, and nothing moves.
	const ECataclysmForgeResult Refused =
		UCataclysmForgeCraft::Imprint(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	TestEqual(TEXT("a second imprint at +4 is refused"), static_cast<int32>(Refused),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying so"), Message,
			  FString(TEXT("A Mythical piece needs gear level 6, and that piece is +4. Upgrade it first.")));
	TestEqual(TEXT("and no shard is used"), Scene.Bag()->CountOfMaterial(Shard), 1);
	AddInfo(FString::Printf(TEXT("IMPRINTED: %s with %s, rolls %.3f and %.3f; day %d to %d; then '%s'"),
							*Expected.Positive.ToString(), *Expected.Negative.ToString(), Expected.PositiveRoll,
							Expected.NegativeRoll, DayBefore, Scene.Run->Day(), *Message));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE5. Changing a drawback: the rule's figures, its question and every refusal.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsChangeRuleTest,
	"Cataclysm.ForgeEnchantments.ChangingADrawbackTakesARegretEssence100ResidueSevenDaysAndNeverASetsPair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Legendary helm with residue 200: a Regret Essence, residue 300, 7 days and 2 more for 200 residue, 1,750 gold times
 * (200 / 50 + 1) = 8,750. REFUSED: no enchantment 3, a set's pair, a positive the table does not hold, no essence, too
 * little gold, no piece.
 */
bool FCataclysmForgeEnchantmentsChangeRuleTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Positives = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const FCataclysmItem Helm = LegendaryHelm(Pair(MoreHealth, LessEffective));

	const FCataclysmForgeEnchantmentWork Plan = UCataclysmForge::ChangeNegativeOf(Helm, 0, 1, Positives, 1, 1000000);
	TestTrue(TEXT("the helm's drawback can be changed"), Plan.bPossible);
	TestFalse(TEXT("it is no imprint"), Plan.bImprint);
	TestTrue(TEXT("the pair before"), Plan.Before.Positive == MoreHealth && Plan.Before.Negative == LessEffective);
	TestEqual(TEXT("with a Regret Essence"), Plan.Material, Essence);
	TestEqual(TEXT("residue 200 becomes 300"), Plan.ResidueAfter, 300.0f, 0.001f);
	TestEqual(TEXT("7 days and 2 for 200 residue"), Plan.Days, 9);
	TestEqual(TEXT("1,750 gold a time, at 200 residue 8,750"), Plan.Gold, static_cast<int64>(8750));
	TestEqual(TEXT("the first press's question"),
			  UCataclysmForge::EnchantmentWorkQuestion(Plan, TEXT("Gain 20% increased health")),
			  FString(TEXT("Press again to change the drawback of Gain 20% increased health: one Regret Essence, 8,750 "
						   "gold, residue 200 to 300, 9 days of the run; a different drawback is drawn at random.")));

	TestEqual(TEXT("no enchantment 3"), UCataclysmForge::ChangeNegativeOf(Helm, 2, 1, Positives, 1, 1000000).Refusal,
			  FString(TEXT("That piece has no enchantment 3.")));
	TestEqual(TEXT("a set's pair"),
			  UCataclysmForge::ChangeNegativeOf(LegendaryHelm(Pair(ArchonsAegis, NoArmour)), 0, 1, Positives, 1, 1000000)
				  .Refusal,
			  FString(TEXT("A set's drawback is part of the set; it is not changed.")));
	TestEqual(TEXT("a positive the table does not hold"),
			  UCataclysmForge::ChangeNegativeOf(LegendaryHelm(Pair(FName(TEXT("Positive_No_Such_Row")), NoArmour)), 0, 1,
												Positives, 1, 1000000)
				  .Refusal,
			  FString(TEXT("That enchantment is not in the enchantment table, so its drawback cannot be redrawn.")));
	TestEqual(TEXT("no Regret Essence"), UCataclysmForge::ChangeNegativeOf(Helm, 0, 0, Positives, 1, 1000000).Refusal,
			  FString(TEXT("Changing an enchantment's drawback takes a Regret Essence; 0 held.")));
	TestEqual(TEXT("too little gold"), UCataclysmForge::ChangeNegativeOf(Helm, 0, 1, Positives, 1, 100).Refusal,
			  FString(TEXT("Changing an enchantment's drawback takes 8,750 gold; 100 gold held.")));
	TestEqual(TEXT("no piece"), UCataclysmForge::ChangeNegativeOf(FCataclysmItem(), 0, 1, Positives, 1, 1000000).Refusal,
			  FString(TEXT("There is nothing there to change.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE6. A changed drawback's draw: different, at or below its positive's weight, a new roll, the positive kept.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsChangeDrawTest,
	"Cataclysm.ForgeEnchantments.AChangedDrawbackIsADifferentOneNeverMilderThanItsPositive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A helm holding two pairs, "Gain 10-30% increased health" (weight 3) with "Gems ... 50% less effective" and "Double your
 * energy shield" (weight 1) with "You have no armor", the first pair's drawback drawn on days 1 to 400: never the one
 * replaced (Q18), never the other pair's, never weighing more than 3; the positive and its roll kept; the same day draws
 * the same. The weight 1 pair's drawback is always weight 1, the only band at or below it. THE CONTROL: the draws differ
 * across the days and reach weights 1, 2 and 3. Prints "CHANGE DRAWS".
 */
bool FCataclysmForgeEnchantmentsChangeDrawTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Positives = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const UDataTable* Negatives = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	FCataclysmItem Helm = LegendaryHelm(Pair(MoreHealth, LessEffective));
	Helm.GearLevel = 6;
	Helm.Affixes.SetNum(2);
	Helm.Enchantments.Add(Pair(DoubleShield, NoArmour));
	Helm.EnchantmentCount = 2;
	constexpr int32 Mask = 54321;

	int32 Drawn = 0;
	int32 Same = 0;
	int32 TheOthers = 0;
	int32 Heavier = 0;
	int32 PositiveMoved = 0;
	int32 Unstable = 0;
	int32 WeightOneMilder = 0;
	int32 ByWeight[5] = {0, 0, 0, 0, 0};
	TSet<FName> Distinct;
	for (int32 Day = 1; Day <= 400; ++Day)
	{
		FCataclysmRolledEnchantment First;
		FCataclysmRolledEnchantment Again;
		FCataclysmRolledEnchantment Second;
		if (!UCataclysmForge::DrawChangedNegative(Helm, 0, Bases, Positives, Negatives, Mask, Day, First)
			|| !UCataclysmForge::DrawChangedNegative(Helm, 0, Bases, Positives, Negatives, Mask, Day, Again))
		{
			continue;
		}
		++Drawn;
		Same += First.Negative == LessEffective ? 1 : 0;
		TheOthers += First.Negative == NoArmour ? 1 : 0;
		const float Weight = WeightOf(Negatives, First.Negative);
		Heavier += Weight > 3.0f ? 1 : 0;
		ByWeight[FMath::Clamp(FMath::RoundToInt(Weight), 0, 4)] += 1;
		PositiveMoved += (First.Positive != MoreHealth || First.PositiveRoll != 0.5f) ? 1 : 0;
		Unstable += (First.Negative != Again.Negative || First.NegativeRoll != Again.NegativeRoll) ? 1 : 0;
		Distinct.Add(First.Negative);
		if (UCataclysmForge::DrawChangedNegative(Helm, 1, Bases, Positives, Negatives, Mask, Day, Second))
		{
			WeightOneMilder += WeightOf(Negatives, Second.Negative) != 1.0f || Second.Negative == NoArmour ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("CHANGE DRAWS: %d of 400, %d distinct; by weight 1:%d 2:%d 3:%d 4:%d; same %d, the "
								 "other pair's %d, heavier %d, positive moved %d, unstable %d, weight 1 pair wrong %d"),
							Drawn, Distinct.Num(), ByWeight[1], ByWeight[2], ByWeight[3], ByWeight[4], Same, TheOthers,
							Heavier, PositiveMoved, Unstable, WeightOneMilder));
	TestEqual(TEXT("every day draws"), Drawn, 400);
	TestEqual(TEXT("never the drawback replaced"), Same, 0);
	TestEqual(TEXT("never the other pair's"), TheOthers, 0);
	TestEqual(TEXT("never above the positive's weight 3"), Heavier, 0);
	TestEqual(TEXT("the positive and its roll kept"), PositiveMoved, 0);
	TestEqual(TEXT("the same day draws the same"), Unstable, 0);
	TestEqual(TEXT("the weight 1 pair's drawback is always a different weight 1 row"), WeightOneMilder, 0);
	TestTrue(TEXT("control: the days draw different drawbacks"), Distinct.Num() > 40);
	TestTrue(TEXT("control: weights 1, 2 and 3 are all reached"), ByWeight[1] > 0 && ByWeight[2] > 0 && ByWeight[3] > 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE7. The change craft on a carried helm.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsChangeCraftTest,
	"Cataclysm.ForgeEnchantments.TheChangeCraftReplacesOnlyTheDrawbackWithTheOneItsDayDraws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The helm's drawback is changed: the essence is used, 8,750 gold paid, 9 days pass, residue 300, the positive and its
 * roll are kept, and the drawback is the one `UCataclysmForge::DrawChangedNegative` gives for the run's mask and that
 * day. The affixes and sockets are as they were. Prints "CHANGED".
 */
bool FCataclysmForgeEnchantmentsChangeCraftTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FForgeEnchantmentsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmItem Helm = LegendaryHelm(Pair(MoreHealth, NoArmour));
	const int32 Cell = Scene.Bag()->AddItem(Helm);
	Scene.Bag()->AddMaterial(Essence, 1);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	FCataclysmRolledEnchantment Expected;
	TestTrue(TEXT("set-up: the day's draw is known"),
			 UCataclysmForge::DrawChangedNegative(Helm, 0, UCataclysmItemModifiers::LoadBaseTable(),
												  UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
												  UCataclysmDropRoll::LoadNegativeEnchantmentTable(),
												  Scene.Run->WalkSeedMask, DayBefore, Expected));

	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::ChangeNegative(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the change is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After)
		|| !TestEqual(TEXT("it holds one pair"), After->Enchantments.Num(), 1))
	{
		AddError(Message);
		return false;
	}
	const FCataclysmRolledEnchantment& Now = After->Enchantments[0];
	TestTrue(TEXT("the positive and its roll kept"), Now.Positive == MoreHealth && Now.PositiveRoll == 0.5f);
	TestTrue(TEXT("a different drawback"), Now.Negative != NoArmour);
	TestTrue(TEXT("the one the day draws, with its roll"),
			 Now.Negative == Expected.Negative && Now.NegativeRoll == Expected.NegativeRoll);
	TestTrue(TEXT("the one the craft reports"), Done.Drawn.Negative == Now.Negative);
	TestEqual(TEXT("the affixes as they were"), After->Affixes.Num(), 3);
	TestTrue(TEXT("and the socket"), After->Sockets.Num() == 1 && After->Sockets[0].Gem == OfLife);
	TestEqual(TEXT("residue 200 becomes 300"), After->Residue, 300.0f, 0.001f);
	TestEqual(TEXT("the Regret Essence is used"), Scene.Bag()->CountOfMaterial(Essence), 0);
	TestEqual(TEXT("nine days passed"), Scene.Run->Day(), DayBefore + 9);
	TestEqual(TEXT("8,750 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(8750));
	AddInfo(FString::Printf(TEXT("CHANGED: %s to %s, roll %.3f; day %d to %d"), *NoArmour.ToString(),
							*Now.Negative.ToString(), Now.NegativeRoll, DayBefore, Scene.Run->Day()));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE8. G10 end to end: an imprint that draws fit-any lets an off-type gem in at the forge, and the gem acts.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsImprintFitsAnyTest,
	"Cataclysm.ForgeEnchantments.AnImprintedFitAnyLetsAnOffTypeGemInAtTheForgeAndItActs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE DRAW IS RANDOM, SO THE TEST FINDS A SWORD IT DRAWS FIT-ANY FOR: the sword's fourth affix roll is stepped until
 * `UCataclysmForge::DrawImprint` gives "Sockets on this equipment fit any gem type" without "Gems ... 50% less
 * effective" for the run's day (fit-any is about 1 imprint in 600 on a weapon). Then, all through the crafts: Of Life, a
 * Defense gem, is refused by the sword's Attack socket; the sword is imprinted; Of Life is socketed into the same socket;
 * and it adds its Superb 25 increased maximum health. Prints "FIT-ANY IMPRINTED".
 */
bool FCataclysmForgeEnchantmentsImprintFitsAnyTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FForgeEnchantmentsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCataclysmItem Sword = MasterfulSword();
	int32 Step = 0;
	bool bFound = false;
	for (; Step < 20000 && !bFound; ++Step)
	{
		Sword.Affixes[3].Roll = static_cast<float>(Step) / 20000.0f;
		FCataclysmRolledEnchantment Drawn;
		FRandomStream Unused;
		bFound = UCataclysmForge::DrawImprint(Sword, UCataclysmItemModifiers::LoadBaseTable(),
											  UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
											  UCataclysmDropRoll::LoadNegativeEnchantmentTable(),
											  Scene.Run->WalkSeedMask, Scene.Run->Day(), Drawn, Unused)
			&& Drawn.Positive == FitsAny && Drawn.Negative != LessEffective;
	}
	if (!TestTrue(TEXT("set-up: some roll of the fourth affix draws fit-any within 20,000 steps"), bFound))
	{
		return false;
	}
	const int32 Cell = Scene.Bag()->AddItem(Sword);
	Scene.Bag()->AddMaterial(Shard, 1);
	Scene.Bag()->AddMaterial(Agent, 2);
	Scene.Bag()->AddGems(OfLife, ECataclysmRarity::Superb, 2);

	FCataclysmForgeSocketing Socketed;
	FString Before;
	const ECataclysmForgeResult Refused = UCataclysmForgeCraft::SocketGem(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, OfLife, ECataclysmRarity::Superb, false, Socketed, Before);
	TestEqual(TEXT("control: before the imprint, Of Life does not go into the Attack socket"),
			  static_cast<int32>(Refused), static_cast<int32>(ECataclysmForgeResult::Refused));

	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Imprinted =
		UCataclysmForgeCraft::Imprint(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	TestEqual(TEXT("the imprint is done"), static_cast<int32>(Imprinted), static_cast<int32>(ECataclysmForgeResult::Done));
	TestTrue(TEXT("drawing fit-any"), Done.Drawn.Positive == FitsAny);

	FString After;
	const ECataclysmForgeResult Into = UCataclysmForgeCraft::SocketGem(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, OfLife, ECataclysmRarity::Superb, false, Socketed, After);
	TestEqual(TEXT("after it, Of Life goes into the Attack socket"), static_cast<int32>(Into),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	const FCataclysmItem* Now = Scene.Bag()->ItemAt(Cell);
	if (!TestNotNull(TEXT("the sword is still in its cell"), Now))
	{
		return false;
	}
	TestTrue(TEXT("Of Life sits in the Attack socket"), Now->Sockets.Num() == 1 && Now->Sockets[0].Gem == OfLife
												   && Now->Sockets[0].Type == ECataclysmSocketType::Attack);
	const float Life = GemValueOn(*Now, TEXT("max_health"));
	TestEqual(TEXT("and adds its Superb 25 increased maximum health"), Life, 25.0f, 0.0001f);
	AddInfo(FString::Printf(TEXT("FIT-ANY IMPRINTED: step %d, drawback %s; refused before '%s'; after %.3f"), Step - 1,
							*Done.Drawn.Negative.ToString(), *Before, Life));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE9. G10 end to end: changing "50% less effective" away makes the piece's gem act in full.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsLessEffectiveAwayTest,
	"Cataclysm.ForgeEnchantments.ChangingLessEffectiveAwayMakesThePiecesGemActInFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Legendary helm carrying "Gems in this equipment are 50% less effective", Of Life in its Defense socket: 12.5. Its
 * drawback is changed at the forge, to a different one by Q18, and Of Life adds its whole 25. Prints "LESS EFFECTIVE
 * AWAY".
 */
bool FCataclysmForgeEnchantmentsLessEffectiveAwayTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FForgeEnchantmentsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmItem Helm = LegendaryHelm(Pair(MoreHealth, LessEffective));
	const float Halved = GemValueOn(Helm, TEXT("max_health"));
	const int32 Cell = Scene.Bag()->AddItem(Helm);
	Scene.Bag()->AddMaterial(Essence, 1);

	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::ChangeNegative(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the change is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	const float Whole = GemValueOn(*After, TEXT("max_health"));
	AddInfo(FString::Printf(TEXT("LESS EFFECTIVE AWAY: before %.3f; drawback now %s; after %.3f"), Halved,
							*Done.Drawn.Negative.ToString(), Whole));
	TestEqual(TEXT("before: Of Life at half, 12.5"), Halved, 12.5f, 0.0001f);
	TestTrue(TEXT("the drawback is no longer 50% less effective"), After->Enchantments[0].Negative != LessEffective);
	TestEqual(TEXT("after: Of Life whole, 25"), Whole, 25.0f, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE10. G10 end to end: a change that draws "50% less effective" halves the piece's gem.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsLessEffectiveDrawnTest,
	"Cataclysm.ForgeEnchantments.AChangeThatDrawsLessEffectiveHalvesThePiecesGem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE DRAW IS RANDOM, SO THE TEST FINDS A HELM IT DRAWS IT FOR: the helm's third affix roll is stepped until
 * `UCataclysmForge::DrawChangedNegative` gives "Gems in this equipment are 50% less effective" for the run's day (about
 * 1 change in 95 for a weight 3 positive). Then the craft changes the helm's "You have no armor": Of Life went from 25
 * to 12.5. Prints "LESS EFFECTIVE DRAWN".
 */
bool FCataclysmForgeEnchantmentsLessEffectiveDrawnTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FForgeEnchantmentsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCataclysmItem Helm = LegendaryHelm(Pair(MoreHealth, NoArmour));
	int32 Step = 0;
	bool bFound = false;
	for (; Step < 20000 && !bFound; ++Step)
	{
		Helm.Affixes[2].Roll = static_cast<float>(Step) / 20000.0f;
		FCataclysmRolledEnchantment Drawn;
		bFound = UCataclysmForge::DrawChangedNegative(Helm, 0, UCataclysmItemModifiers::LoadBaseTable(),
													  UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
													  UCataclysmDropRoll::LoadNegativeEnchantmentTable(),
													  Scene.Run->WalkSeedMask, Scene.Run->Day(), Drawn)
			&& Drawn.Negative == LessEffective;
	}
	if (!TestTrue(TEXT("set-up: some roll of the third affix draws 50% less effective within 20,000 steps"), bFound))
	{
		return false;
	}
	const float Whole = GemValueOn(Helm, TEXT("max_health"));
	const int32 Cell = Scene.Bag()->AddItem(Helm);
	Scene.Bag()->AddMaterial(Essence, 1);

	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::ChangeNegative(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the change is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	const float Halved = GemValueOn(*After, TEXT("max_health"));
	AddInfo(FString::Printf(TEXT("LESS EFFECTIVE DRAWN: step %d; before %.3f; drawback now %s; after %.3f"), Step - 1,
							Whole, *After->Enchantments[0].Negative.ToString(), Halved));
	TestEqual(TEXT("before: Of Life whole, 25"), Whole, 25.0f, 0.0001f);
	TestTrue(TEXT("the drawback is now 50% less effective"), After->Enchantments[0].Negative == LessEffective);
	TestEqual(TEXT("after: Of Life at half, 12.5"), Halved, 12.5f, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE11. The forge screen's buttons.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsScreenTest,
	"Cataclysm.ForgeEnchantments.TheScreenOffersAnImprintPerAffixAndADrawbackChangePerEnchantmentButASets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Masterful sword is offered four imprints, "Imprint, giving up affix 1" to "affix 4", numbered as slice F1's affix
 * list in the screen's detail (`UCataclysmForgeLayout::AffixesDetailFor`), and no drawback change. A Mythical helm holding a set's pair and an ordinary pair is offered two imprints and one drawback change,
 * the ordinary pair's. A Superb piece is offered nothing, as `UCataclysmForge::ImprintOf` refuses it. Prints "SCREEN".
 */
bool FCataclysmForgeEnchantmentsScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	const UDataTable* Positives = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const UDataTable* Negatives = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	const FCataclysmItem Sword = MasterfulSword();

	const TArray<FCataclysmForgeEnchantmentAction> OnSword =
		UCataclysmForgeLayout::EnchantmentActionsFor(Sword, Positives, Negatives);
	TestEqual(TEXT("four imprints on the Masterful sword"), OnSword.Num(), 4);
	for (int32 Index = 0; Index < OnSword.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("imprint %d gives up affix %d"), Index + 1, Index + 1),
				 OnSword[Index].bImprint && OnSword[Index].Index == Index);
		TestEqual(FString::Printf(TEXT("imprint %d's label"), Index + 1), OnSword[Index].Label,
				  FString::Printf(TEXT("Imprint, giving up affix %d"), Index + 1));
		TestEqual(FString::Printf(TEXT("imprint %d's question names"), Index + 1), OnSword[Index].Named,
				  FString::Printf(TEXT("affix %d"), Index + 1));
	}
	TestTrue(TEXT("slice F1's detail lists the affixes by the same numbers"),
			 UCataclysmForgeLayout::AffixesDetailFor(Sword).StartsWith(TEXT("Affixes: 1. ")));

	FCataclysmItem Helm = LegendaryHelm(Pair(ArchonsAegis, NoArmour));
	Helm.GearLevel = 6;
	Helm.Affixes.SetNum(2);
	Helm.Enchantments.Add(Pair(MoreHealth, LessEffective));
	Helm.EnchantmentCount = 2;
	const TArray<FCataclysmForgeEnchantmentAction> OnHelm =
		UCataclysmForgeLayout::EnchantmentActionsFor(Helm, Positives, Negatives);
	const TArray<FString> HealthLines = UCataclysmItemTooltip::EnchantmentLines(Helm.Enchantments[1], Positives, Negatives);
	TestEqual(TEXT("two imprints and one drawback change on the Mythical helm"), OnHelm.Num(), 3);
	if (OnHelm.Num() == 3)
	{
		TestTrue(TEXT("the change is the ordinary pair's, the second"), !OnHelm[2].bImprint && OnHelm[2].Index == 1);
		TestEqual(TEXT("its button"), OnHelm[2].ButtonName, FName(TEXT("ChangeDrawback_1")));
		TestEqual(TEXT("its label"), OnHelm[2].Label,
				  FString(TEXT("Change the drawback of ")) + (HealthLines.Num() > 0 ? HealthLines[0] : FString()));
	}

	FCataclysmItem Superb = Sword;
	Superb.Affixes.SetNum(3);
	TestEqual(TEXT("nothing on a Superb piece"),
			  UCataclysmForgeLayout::EnchantmentActionsFor(Superb, Positives, Negatives).Num(), 0);
	AddInfo(FString::Printf(TEXT("SCREEN: sword %d [%s]; helm %d [%s]"), OnSword.Num(),
							OnSword.Num() > 0 ? *OnSword[0].Label : TEXT(""), OnHelm.Num(),
							OnHelm.Num() > 2 ? *OnHelm[2].Label : TEXT("")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FE12. An imprint can draw a set (ruled 2026-11-22, F3-Q2), and an imprinted set piece counts toward its set's bonus
// as a dropped one does.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeEnchantmentsImprintedSetTest,
	"Cataclysm.ForgeEnchantments.AnImprintedSetPieceCountsTowardItsSetsBonusAsADroppedOneDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE DRAW IS RANDOM, SO THE TEST FINDS A SWORD IT DRAWS A SET FOR: the sword's fourth affix roll is stepped until
 * `UCataclysmForge::DrawImprint` gives a set's row for the run's day (all sets together are about 1 imprint in 150).
 * The craft imprints it: the pair is the set's row and the set's own drawback, never a drawn one. Then, worn: the
 * imprinted sword alone grants fewer modifiers than it does beside a second piece of the set, so the 2-piece bonus
 * joins at two; and a piece built with the same pair, as a drop makes it, in the imprinted sword's place grants exactly
 * the same modifiers. Prints "IMPRINTED SET".
 */
bool FCataclysmForgeEnchantmentsImprintedSetTest::RunTest(const FString&)
{
	using namespace CataclysmForgeEnchantmentsTest;
	if (!TheTables(*this))
	{
		return false;
	}
	FForgeEnchantmentsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const UDataTable* Positives = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const UDataTable* Negatives = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	TArray<FCataclysmEnchantmentSet> Sets;
	UCataclysmDropRoll::EnchantmentSetsFor(Positives, Negatives, TEXT("Weapon"), Sets);
	if (!TestTrue(TEXT("set-up: a weapon can be a piece of some set"), Sets.Num() > 0))
	{
		return false;
	}
	FCataclysmItem Sword = MasterfulSword();
	FCataclysmRolledEnchantment Drawn;
	const FCataclysmEnchantmentSet* DrawnSet = nullptr;
	int32 Step = 0;
	for (; Step < 20000 && DrawnSet == nullptr; ++Step)
	{
		Sword.Affixes[3].Roll = static_cast<float>(Step) / 20000.0f;
		FRandomStream Unused;
		if (UCataclysmForge::DrawImprint(Sword, UCataclysmItemModifiers::LoadBaseTable(), Positives, Negatives,
										 Scene.Run->WalkSeedMask, Scene.Run->Day(), Drawn, Unused))
		{
			DrawnSet = Sets.FindByPredicate([&Drawn](const FCataclysmEnchantmentSet& Each)
			{
				return Each.Representative == Drawn.Positive;
			});
		}
	}
	if (!TestNotNull(TEXT("set-up: some roll of the fourth affix draws a set within 20,000 steps"), DrawnSet))
	{
		return false;
	}
	TestEqual(TEXT("the drawn set brings its own drawback, never a drawn one"), Drawn.Negative, DrawnSet->Negative);

	const int32 Cell = Scene.Bag()->AddItem(Sword);
	Scene.Bag()->AddMaterial(Shard, 1);
	FCataclysmForgeEnchantmentWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Imprint(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	const FCataclysmItem* Imprinted = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the imprint is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the sword is still in its cell"), Imprinted)
		|| !TestEqual(TEXT("it holds one pair"), Imprinted->Enchantments.Num(), 1))
	{
		AddError(Message);
		return false;
	}
	TestTrue(TEXT("the set's row and its own drawback"), Imprinted->Enchantments[0].Positive == DrawnSet->Representative
		&& Imprinted->Enchantments[0].Negative == DrawnSet->Negative);

	// A SECOND PIECE OF THE SET, AND A COPY OF THE IMPRINTED SWORD BUILT AS A DROP BUILDS IT: the same pair and rolls on
	// a Legendary sword that was never at the forge.
	FCataclysmItem Dropped = MasterfulSword();
	Dropped.Affixes.SetNum(3);
	Dropped.Enchantments = {Imprinted->Enchantments[0]};
	Dropped.EnchantmentCount = 1;
	FCataclysmItem DroppedCopy = *Imprinted;
	DroppedCopy.Residue = 0.0f;

	const UDataTable* Effects = UCataclysmItemModifiers::LoadEnchantmentEffectTable();
	const auto Granted = [&](const TArray<FCataclysmItem>& Worn, TMap<FName, TArray<FCataclysmStatModifier>>& Totals)
	{
		return UCataclysmItemModifiers::AccumulateEnchantmentsInto(Totals, Worn, Effects, Positives, Negatives);
	};
	TMap<FName, TArray<FCataclysmStatModifier>> AloneTotals;
	TMap<FName, TArray<FCataclysmStatModifier>> PairTotals;
	TMap<FName, TArray<FCataclysmStatModifier>> DroppedTotals;
	const int32 Alone = Granted({*Imprinted}, AloneTotals);
	const int32 WithASecond = Granted({*Imprinted, Dropped}, PairTotals);
	const int32 BothDropped = Granted({DroppedCopy, Dropped}, DroppedTotals);
	AddInfo(FString::Printf(TEXT("IMPRINTED SET: step %d, set %d (%s, %s); modifiers alone %d, with a second piece %d, "
								 "with a dropped copy in its place %d"),
							Step - 1, DrawnSet->SetId, *DrawnSet->Representative.ToString(),
							*DrawnSet->Negative.ToString(), Alone, WithASecond, BothDropped));
	TestTrue(TEXT("beside a second piece the set grants more: its 2-piece bonus joins at two"), WithASecond > Alone);
	TestEqual(TEXT("a dropped copy in the imprinted sword's place grants the same count"), BothDropped, WithASecond);
	bool bSameStats = PairTotals.Num() == DroppedTotals.Num();
	for (const TPair<FName, TArray<FCataclysmStatModifier>>& Each : PairTotals)
	{
		const TArray<FCataclysmStatModifier>* Other = DroppedTotals.Find(Each.Key);
		bSameStats = bSameStats && Other != nullptr && Other->Num() == Each.Value.Num();
	}
	TestTrue(TEXT("on the same stats"), bSameStats);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
