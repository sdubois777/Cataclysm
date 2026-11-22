// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Interface/CataclysmForgeWidget.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "DayClock/CataclysmDayClock.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE FORGE'S DETERMINISTIC AFFIX AND AFFIX SHIFTING. Issue #47, slice F2, ruled by the coordinating session on
 * 2026-11-22.
 *
 * The Crafting sheet's rows: Deterministic Affix, Schematic Fragments, 5 residue and one base day for each tier of the
 * affix added, Base Gold 250; Affix Shifting, a Crystal of Instability, 15 residue, 1 base day, Base Gold 250. An
 * addition takes the affix and tier chosen, needs a free slot of the four and promotes the piece one rarity; a shift
 * draws another affix from the same position's pool and keeps the tier and roll. Both draws come from the item, the
 * run's mask and the day. The scene is `CataclysmForgeAffixesTests.cpp`'s, copied into this file's namespace because
 * the module is built in unity files. The fixed figures were computed with the Python copy in
 * `tools/tests/test_the_forge_adds_and_shifts_affixes.py`.
 */
namespace CataclysmForgeAffixChangeTest
{
	const FName Fragments(UCataclysmForge::SchematicFragmentsRow);
	const FName Crystal(UCataclysmForge::CrystalOfInstabilityRow);
	const FName BlockChance(TEXT("Stat_Flat_block_chance"));
	const FName FlatArmor(TEXT("Stat_Flat_armor"));
	const FName FlatEvasion(TEXT("Stat_Flat_evasion"));
	const FName TwoResistances(TEXT("Resistance_Two_resistances"));

	FCataclysmRolledAffix MakeAffix(const TCHAR* Row, int32 Tier, float Roll, const TArray<FName>& Types = {})
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Row);
		Out.Tier = Tier;
		Out.Roll = Roll;
		Out.DamageTypes = Types;
		return Out;
	}

	/** THE FIXED HELM: gear level 3, flat evasion at tier 2 rolled 0.5 and flat armor at tier 4 rolled 0.25; Quality. */
	FCataclysmItem Helm(float Residue = 40.0f)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = 3;
		Item.Affixes = {MakeAffix(TEXT("Stat_Flat_evasion"), 2, 0.5f), MakeAffix(TEXT("Stat_Flat_armor"), 4, 0.25f)};
		Item.Residue = Residue;
		return Item;
	}

	/** The fixed helm with a single resistance to War in place of its flat armor. */
	FCataclysmItem ResistantHelm()
	{
		FCataclysmItem Item = Helm();
		Item.Affixes[1] = MakeAffix(TEXT("Resistance_Single_resistance"), 1, 0.5f, {FName(TEXT("War"))});
		return Item;
	}

	struct FAffixChangeScene
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
		UCataclysmEquipmentComponent* Worn() const { return Character->GetEquipment(); }
	};

	/** A world with this project's game instance, a possessed player with a million gold of its own, and a run. */
	FAffixChangeScene MakeScene()
	{
		FAffixChangeScene Out;
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
			Out.State->SetIdentity(TEXT("Changer"), Partition);
			Out.State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2847, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}

	ECataclysmGearSlot Wear(const FAffixChangeScene& Scene, const FCataclysmItem& Item)
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Scene.Worn()->Equip(Item, First, Second, Went);
		return Went;
	}

	FString Labels(const TArray<FCataclysmForgeAffixChangeAction>& Actions)
	{
		TArray<FString> Out;
		for (const FCataclysmForgeAffixChangeAction& Action : Actions)
		{
			Out.Add(Action.Label);
		}
		return FString::Join(Out, TEXT(" | "));
	}
}

// --------------------------------------------------------------------------------------------------------------------
// FC1. Adding an affix: the rule's figures, its question and refusals, and the craft on a carried helm.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixChangeAddTest,
	"Cataclysm.ForgeAffixChange.AddingAnAffixTakesSchematicFragmentsFiveResidueADayATierAndPromotesThePiece",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixChangeAddTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixChangeTest;
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the affix table"), Affixes) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	const FCataclysmItem Item = Helm();

	const FCataclysmForgeAffixChange Plan =
		UCataclysmForge::DeterministicAffixOf(Item, BlockChance, 3, 1655429928, Affixes, Bases, 1, 1, 1000000);
	TestTrue(TEXT("flat block chance can be added to the helm at tier 3"), Plan.bPossible);
	TestEqual(TEXT("as affix 3"), Plan.Affix, 2);
	TestEqual(TEXT("with Schematic Fragments"), Plan.Material, Fragments);
	TestEqual(TEXT("5 residue a tier: 40 becomes 55"), Plan.ResidueAfter, 55.0f, 0.001f);
	TestEqual(TEXT("a day a tier: 3"), Plan.Days, 3);
	TestEqual(TEXT("250 gold flat, at 40 residue 450"), Plan.Gold, static_cast<int64>(450));
	TestTrue(TEXT("the helm was Quality and becomes Superb"),
			 Plan.bHadRarity && Plan.FromRarity == ECataclysmRarity::Quality && Plan.ToRarity == ECataclysmRarity::Superb);
	TestEqual(TEXT("the first press's question"), UCataclysmForge::AffixChangeQuestion(Plan, Affixes),
			  FString(TEXT("Press again to add Flat block chance at tier 3 as affix 3, making the piece Superb: Schematic "
						   "Fragments, 450 gold, residue 40 to 55, 3 days of the run; its roll is drawn in the tier.")));
	TestEqual(TEXT("its tier 7 base days are 7"),
			  UCataclysmForge::DeterministicAffixOf(Item, BlockChance, 7, 1, Affixes, Bases, 1, 1, 1000000).Days, 7);

	const auto Refusal = [&](const FCataclysmItem& On, FName Row, int32 Tier, int32 Held, int64 Gold)
	{
		return UCataclysmForge::DeterministicAffixOf(On, Row, Tier, 1, Affixes, Bases, Held, 1, Gold).Refusal;
	};
	TestEqual(TEXT("tier 0"), Refusal(Item, BlockChance, 0, 1, 1000000), FString(TEXT("An affix can be added at tier 1 to 7.")));
	TestEqual(TEXT("tier 8"), Refusal(Item, BlockChance, 8, 1, 1000000), FString(TEXT("An affix can be added at tier 1 to 7.")));
	TestEqual(TEXT("a stat group the helm holds"), Refusal(Item, FlatArmor, 3, 1, 1000000),
			  FString(TEXT("Flat armor cannot be added to that piece.")));
	TestEqual(TEXT("an affix no helm can roll"), Refusal(Item, FName(TEXT("Stat_Flat_damage")), 3, 1, 1000000),
			  FString(TEXT("Flat damage cannot be added to that piece.")));
	FCataclysmItem Full = Helm();
	Full.Affixes.Add(MakeAffix(TEXT("Stat_Flat_maximum_health"), 1, 0.5f));
	Full.Affixes.Add(MakeAffix(TEXT("Stat_Flat_retaliation"), 1, 0.5f));
	TestEqual(TEXT("a Masterful piece's four slots"), Refusal(Full, BlockChance, 3, 1, 1000000),
			  FString(TEXT("That piece's four slots are full.")));
	FCataclysmItem Enchanted = Helm();
	Enchanted.Affixes.Add(MakeAffix(TEXT("Stat_Flat_maximum_health"), 1, 0.5f));
	Enchanted.EnchantmentCount = 1;
	TestEqual(TEXT("a Legendary piece, its enchantment in the fourth"), Refusal(Enchanted, BlockChance, 3, 1, 1000000),
			  FString(TEXT("That piece's four slots are full.")));
	TestEqual(TEXT("no Schematic Fragments"), Refusal(Item, BlockChance, 3, 0, 1000000),
			  FString(TEXT("Adding an affix takes Schematic Fragments; 0 held.")));
	TestEqual(TEXT("too little gold"), Refusal(Item, BlockChance, 3, 1, 100),
			  FString(TEXT("Adding an affix takes 450 gold; 100 gold held.")));
	TestEqual(TEXT("no piece"), Refusal(FCataclysmItem(), BlockChance, 3, 1, 1000000),
			  FString(TEXT("There is nothing there to craft.")));

	FAffixChangeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Item);
	Scene.Bag()->AddMaterial(Fragments, 1);
	const FCataclysmItem Before = *Scene.Bag()->ItemAt(Cell);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	const float Expected = UCataclysmForge::DeterministicAffixOf(
		Before, BlockChance, 3,
		UCataclysmForge::AffixCraftSeed(Before, UCataclysmForge::DeterministicAffixSalt, 2, Scene.Run->WalkSeedMask, DayBefore),
		Affixes, Bases, 1, 1, 1000000).Roll;

	FCataclysmForgeAffixChange Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::AddAffix(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), BlockChance, 3, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the affix is added"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After) || !TestEqual(TEXT("with 3 affixes"), After->Affixes.Num(), 3))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("ADDED: %s tier %d roll %.6f (expected %.6f), residue %.0f, day %d to %d, gold %lld"),
							*After->Affixes[2].Affix.ToString(), After->Affixes[2].Tier, After->Affixes[2].Roll, Expected,
							After->Residue, DayBefore, Scene.Run->Day(), GoldBefore - Scene.State->GetOwnGold()));
	TestTrue(TEXT("affix 3 is flat block chance at tier 3"), After->Affixes[2].Affix == BlockChance && After->Affixes[2].Tier == 3);
	TestEqual(TEXT("its roll is the one drawn for this item, run and day"), After->Affixes[2].Roll, Expected, 0.000001f);
	TestTrue(TEXT("affixes 1 and 2 are untouched"),
			 After->Affixes[0].Affix == FlatEvasion && After->Affixes[0].Roll == 0.5f && After->Affixes[1].Affix == FlatArmor
				 && After->Affixes[1].Tier == 4);
	ECataclysmRarity Now = ECataclysmRarity::Everyday;
	TestTrue(TEXT("the helm is Superb"), UCataclysmItemModifiers::RarityOfItem(*After, Now) && Now == ECataclysmRarity::Superb);
	TestEqual(TEXT("residue 40 becomes 55"), After->Residue, 55.0f, 0.001f);
	TestEqual(TEXT("the Schematic Fragments are used"), Scene.Bag()->CountOfMaterial(Fragments), 0);
	TestEqual(TEXT("three days passed"), Scene.Run->Day(), DayBefore + 3);
	TestEqual(TEXT("450 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(450));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FC2. An addition's draw: the affixes a helm can take, the roll, and a resistance's damage types among those the piece
// does not hold. Every figure computed in Python.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixChangeDrawTest,
	"Cataclysm.ForgeAffixChange.AnAddedAffixsRollAndResistanceTypesAreDrawnFromTheItemTheRunAndTheDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixChangeDrawTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixChangeTest;
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the affix table"), Affixes) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	constexpr int32 Mask = 12345;
	constexpr int32 Day = 7;
	const FCataclysmItem Item = Helm();
	const FCataclysmItem Resistant = ResistantHelm();

	const TArray<FName> Addable = UCataclysmForge::AddableAffixesFor(Item, Affixes, Bases);
	AddInfo(FString::Printf(TEXT("ADDABLE: %d for the helm, first %s, last %s; %d with a War resistance"), Addable.Num(),
							Addable.IsEmpty() ? TEXT("none") : *Addable[0].ToString(),
							Addable.IsEmpty() ? TEXT("none") : *Addable.Last().ToString(),
							UCataclysmForge::AddableAffixesFor(Resistant, Affixes, Bases).Num()));
	TestEqual(TEXT("the helm can take 27 affixes, as computed"), Addable.Num(), 27);
	TestFalse(TEXT("never flat armor, which it holds"), Addable.Contains(FlatArmor));
	TestFalse(TEXT("nor a hybrid with flat armor"), Addable.Contains(FName(TEXT("Hybrid_Health_and_armor"))));
	TestTrue(TEXT("first flat maximum health"), Addable.Num() > 0 && Addable[0] == FName(TEXT("Stat_Flat_maximum_health")));
	TestTrue(TEXT("last flat energy shield regeneration"),
			 Addable.Num() > 0 && Addable.Last() == FName(TEXT("Stat_Flat_energy_shield_regeneration")));
	TestEqual(TEXT("with a War resistance in place of flat armor, 28"),
			  UCataclysmForge::AddableAffixesFor(Resistant, Affixes, Bases).Num(), 28);

	const int32 Seed = UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::DeterministicAffixSalt, 2, Mask, Day);
	TestEqual(TEXT("the addition's seed on day 7 of mask 12345"), Seed, 1655429928);
	const FCataclysmForgeAffixChange Block =
		UCataclysmForge::DeterministicAffixOf(Item, BlockChance, 3, Seed, Affixes, Bases, 1, 1, 1000000);
	TestEqual(TEXT("flat block chance draws 0.209570"), Block.Roll, 0.209570050239563f, 0.000001f);
	TestTrue(TEXT("and no damage types"), Block.ToDamageTypes.IsEmpty());
	TestEqual(TEXT("again the same"),
			  UCataclysmForge::DeterministicAffixOf(Item, BlockChance, 3, Seed, Affixes, Bases, 1, 1, 1000000).Roll,
			  Block.Roll, 0.0f);

	const int32 ResistantSeed =
		UCataclysmForge::AffixCraftSeed(Resistant, UCataclysmForge::DeterministicAffixSalt, 2, Mask, Day);
	TestEqual(TEXT("the War helm's seed"), ResistantSeed, 2086854589);
	const FCataclysmForgeAffixChange Two =
		UCataclysmForge::DeterministicAffixOf(Resistant, TwoResistances, 3, ResistantSeed, Affixes, Bases, 1, 1, 1000000);
	AddInfo(FString::Printf(TEXT("TWO RESISTANCES: %s, roll %.6f"),
							*FString::JoinBy(Two.ToDamageTypes, TEXT("+"), [](const FName& Type) { return Type.ToString(); }),
							Two.Roll));
	TestTrue(TEXT("two resistances draw Famine and Celestial"),
			 Two.ToDamageTypes.Num() == 2 && Two.ToDamageTypes[0] == FName(TEXT("Famine"))
				 && Two.ToDamageTypes[1] == FName(TEXT("Celestial")));
	TestEqual(TEXT("then the roll 0.220961"), Two.Roll, 0.2209608554840088f, 0.000001f);

	// NEVER A TYPE THE PIECE HOLDS, over many days.
	bool bEverWar = false;
	for (int32 Each = 1; Each <= 300; ++Each)
	{
		const FCataclysmForgeAffixChange Drawn = UCataclysmForge::DeterministicAffixOf(
			Resistant, TwoResistances, 3,
			UCataclysmForge::AffixCraftSeed(Resistant, UCataclysmForge::DeterministicAffixSalt, 2, Mask, Each), Affixes,
			Bases, 1, 1, 1000000);
		bEverWar |= Drawn.ToDamageTypes.Contains(FName(TEXT("War"))) || Drawn.ToDamageTypes.Num() != 2;
	}
	TestFalse(TEXT("over days 1 to 300 two resistances never cover War, which the helm holds, and always cover two"), bEverWar);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FC3. Shifting an affix: the rule's figures, question and refusals, what it may draw, and the craft on a carried helm.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixChangeShiftTest,
	"Cataclysm.ForgeAffixChange.ShiftingAnAffixTakesACrystalOfInstabilityFifteenResidueADayAndKeepsItsTierAndRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixChangeShiftTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixChangeTest;
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the affix table"), Affixes) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	const FCataclysmItem Item = Helm();
	const int32 Seed = UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::ShiftedAffixSalt, 0, 12345, 7);

	const FCataclysmForgeAffixChange Plan = UCataclysmForge::AffixShiftOf(Item, 0, Seed, Affixes, Bases, 1, 1, 1000000);
	TestTrue(TEXT("affix 1 can be shifted"), Plan.bPossible);
	TestEqual(TEXT("with a Crystal of Instability"), Plan.Material, Crystal);
	TestEqual(TEXT("residue 40 becomes 55"), Plan.ResidueAfter, 55.0f, 0.001f);
	TestEqual(TEXT("one day below 100 residue"), Plan.Days, 1);
	TestEqual(TEXT("250 gold, at 40 residue 450"), Plan.Gold, static_cast<int64>(450));
	TestTrue(TEXT("tier 2 and roll 0.5 are kept"), Plan.Tier == 2 && Plan.Roll == 0.5f);
	TestTrue(TEXT("the rarity is kept"), Plan.FromRarity == Plan.ToRarity);
	TestEqual(TEXT("the first press's question"), UCataclysmForge::AffixChangeQuestion(Plan, Affixes),
			  FString(TEXT("Press again to shift affix 1, Flat evasion, to another stat, keeping tier 2 and its roll: one "
						   "Crystal of Instability, 450 gold, residue 40 to 55, 1 day of the run; the new stat is drawn.")));
	TestTrue(TEXT("the same seed draws the same"),
			 UCataclysmForge::AffixShiftOf(Item, 0, Seed, Affixes, Bases, 1, 1, 1000000).ToRow == Plan.ToRow);
	TestEqual(TEXT("no Crystal of Instability"), UCataclysmForge::AffixShiftOf(Item, 0, Seed, Affixes, Bases, 0, 1, 1000000).Refusal,
			  FString(TEXT("Shifting an affix takes a Crystal of Instability; 0 held.")));
	TestEqual(TEXT("no such affix"), UCataclysmForge::AffixShiftOf(Item, 4, Seed, Affixes, Bases, 1, 1, 1000000).Refusal,
			  FString(TEXT("That piece has no affix 5.")));
	FCataclysmItem Unknown = Helm();
	Unknown.Base = FName(TEXT("No_Such_Base"));
	TestEqual(TEXT("a base not found"), UCataclysmForge::AffixShiftOf(Unknown, 0, Seed, Affixes, Bases, 1, 1, 1000000).Refusal,
			  FString(TEXT("What that piece is cannot be found.")));

	// WHAT A SHIFT MAY DRAW, over 200 days: a prefix a helm can roll, never flat evasion itself, never flat armor or any
	// group the helm holds, and more than a few different ones.
	TSet<FString> Held;
	UCataclysmForge::HeldGroupsOf(Item, Affixes, Held);
	TSet<FName> Seen;
	bool bEverWrong = false;
	for (int32 Each = 1; Each <= 200; ++Each)
	{
		const FCataclysmForgeAffixChange Drawn = UCataclysmForge::AffixShiftOf(
			Item, 0, UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::ShiftedAffixSalt, 0, 12345, Each), Affixes,
			Bases, 1, 1, 1000000);
		const FCataclysmAffixRow* Row = Affixes->FindRow<FCataclysmAffixRow>(Drawn.ToRow, TEXT("FC3"), false);
		TSet<FString> Groups;
		if (Row != nullptr)
		{
			UCataclysmDropRoll::GroupsOf(Affixes, *Row, Drawn.ToDamageTypes, Groups);
		}
		bEverWrong |= !Drawn.bPossible || Row == nullptr || !Row->Position.Equals(TEXT("prefix"), ESearchCase::IgnoreCase)
			|| Drawn.ToRow == FlatEvasion || Groups.Intersect(Held).Num() > 0;
		Seen.Add(Drawn.ToRow);
	}
	AddInfo(FString::Printf(TEXT("SHIFT DRAWS: %d different affixes over 200 days; day 7 drew %s"), Seen.Num(),
							*Plan.ToRow.ToString()));
	TestFalse(TEXT("every draw is a helm prefix of no group the helm holds, never flat evasion itself"), bEverWrong);
	TestTrue(TEXT("and more than five different ones are drawn"), Seen.Num() > 5);

	FAffixChangeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Item);
	Scene.Bag()->AddMaterial(Crystal, 1);
	const FCataclysmItem Before = *Scene.Bag()->ItemAt(Cell);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	const FName Expected = UCataclysmForge::AffixShiftOf(
		Before, 0, UCataclysmForge::AffixCraftSeed(Before, UCataclysmForge::ShiftedAffixSalt, 0, Scene.Run->WalkSeedMask, DayBefore),
		Affixes, Bases, 1, 1, 1000000).ToRow;
	FCataclysmForgeAffixChange Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::ShiftAffix(Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the shift is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After) || !TestEqual(TEXT("with 2 affixes"), After->Affixes.Num(), 2))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("SHIFTED: %s to %s, tier %d, roll %.6f, residue %.0f, day %d to %d"),
							*FlatEvasion.ToString(), *After->Affixes[0].Affix.ToString(), After->Affixes[0].Tier,
							After->Affixes[0].Roll, After->Residue, DayBefore, Scene.Run->Day()));
	TestTrue(TEXT("affix 1 is the affix drawn for this item, run and day"), After->Affixes[0].Affix == Expected);
	TestTrue(TEXT("not flat evasion"), After->Affixes[0].Affix != FlatEvasion);
	TestTrue(TEXT("its tier 2 and roll 0.5 kept"), After->Affixes[0].Tier == 2 && After->Affixes[0].Roll == 0.5f);
	TestTrue(TEXT("affix 2 is untouched"), After->Affixes[1].Affix == FlatArmor && After->Affixes[1].Roll == 0.25f);
	TestEqual(TEXT("residue 40 becomes 55"), After->Residue, 55.0f, 0.001f);
	TestEqual(TEXT("the crystal is used"), Scene.Bag()->CountOfMaterial(Crystal), 0);
	TestEqual(TEXT("a day passed"), Scene.Run->Day(), DayBefore + 1);
	TestEqual(TEXT("450 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(450));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FC4. The screen: a shift for each affix, the chooser stepping through what the piece can take and its tier, two
// presses adding the affix shown, and no chooser on a piece whose four slots are full.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixChangeScreenTest,
	"Cataclysm.ForgeAffixChange.TheScreenShiftsEachAffixAndChoosesAnAffixAndTierToAdd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixChangeScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixChangeTest;
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	FCataclysmItem Full = Helm();
	Full.Affixes.Add(MakeAffix(TEXT("Stat_Flat_maximum_health"), 1, 0.5f));
	Full.Affixes.Add(MakeAffix(TEXT("Stat_Flat_retaliation"), 1, 0.5f));
	const TArray<FName> FullAddable = UCataclysmForge::AddableAffixesFor(Full, Affixes, Bases);
	TestEqual(TEXT("a full piece: a shift for each of its four, no chooser"),
			  Labels(UCataclysmForgeLayout::ShiftActionsFor(Full))
				  + Labels(UCataclysmForgeLayout::AddAffixActionsFor(Full, FullAddable, 0, 1, Affixes)),
			  FString(TEXT("Shift affix 1 | Shift affix 2 | Shift affix 3 | Shift affix 4")));
	TestEqual(TEXT("and the detail says why"), UCataclysmForgeLayout::AddAffixDetailFor(Full, FullAddable, 0, 1, Affixes),
			  FString(TEXT("No affix can be added: that piece's four slots are full.")));

	FAffixChangeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Helm());
	Scene.Bag()->AddMaterial(Fragments, 1);
	UCataclysmForgeWidget* Screen = NewObject<UCataclysmForgeWidget>();
	if (!TestNotNull(TEXT("set-up: the screen was made"), Screen))
	{
		return false;
	}
	Screen->SetWorldContextForTests(Scene.World);
	Screen->RefreshDisplay();
	Screen->ClickPieceForTests(UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Cell)));
	AddInfo(FString::Printf(TEXT("CHANGE BUTTONS: %s; detail: %s"), *Labels(Screen->AffixChangeActionsShown()),
							*Screen->DetailText()));
	TestEqual(TEXT("the helm's shifts and chooser"), Labels(Screen->AffixChangeActionsShown()),
			  FString(TEXT("Shift affix 1 | Shift affix 2 | Previous affix | Next affix | Lower tier | Raise tier | Add Flat "
						   "maximum health at tier 1")));
	TestTrue(TEXT("the detail names the affix to add"),
			 Screen->DetailText().Contains(TEXT("Affix to add: Flat maximum health (prefix), 1 of 27, at tier 1")));

	Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixNextButtonName());
	TestTrue(TEXT("next: increased maximum health"),
			 Labels(Screen->AffixChangeActionsShown()).EndsWith(TEXT("Add Increased maximum health at tier 1")));
	Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixTierUpButtonName());
	Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixTierUpButtonName());
	TestEqual(TEXT("raised twice: tier 3"), Screen->AddAffixTierForTests(), 3);
	for (int32 Press = 0; Press < 3; ++Press)
	{
		Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixTierDownButtonName());
	}
	TestEqual(TEXT("lowered three times: tier 1, not 0"), Screen->AddAffixTierForTests(), 1);
	Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixPreviousButtonName());
	Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixPreviousButtonName());
	TestEqual(TEXT("back twice from the second: the last of 27"), Screen->AddAffixChosenForTests(), 26);
	TestTrue(TEXT("flat energy shield regeneration"),
			 Labels(Screen->AffixChangeActionsShown()).EndsWith(TEXT("Add Flat energy shield regeneration at tier 1")));

	Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixButtonName());
	TestTrue(TEXT("the first press asks"),
			 Screen->MessageText().StartsWith(TEXT("Press again to add Flat energy shield regeneration at tier 1 as affix 3")));
	TestEqual(TEXT("and changes nothing"), Scene.Bag()->ItemAt(Cell)->Affixes.Num(), 2);
	Screen->ClickAffixChangeActionForTests(UCataclysmForgeLayout::AddAffixButtonName());
	AddInfo(FString::Printf(TEXT("SCREEN ADDED: %s"), *Screen->MessageText()));
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestTrue(TEXT("the second press adds it"),
			 After != nullptr && After->Affixes.Num() == 3
				 && After->Affixes[2].Affix == FName(TEXT("Stat_Flat_energy_shield_regeneration"))
				 && After->Affixes[2].Tier == 1);
	TestTrue(TEXT("and says so"), Screen->MessageText().StartsWith(
									  TEXT("Added Flat energy shield regeneration at tier 1 as affix 3; the piece is Superb now.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FC5. A worn piece: an addition that stays below the Consumption Threshold is done at once; a shift that would cross
// it asks, changes nothing, and marks the character only when confirmed.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixChangeThresholdTest,
	"Cataclysm.ForgeAffixChange.AWornAffixChangeThatWouldCrossTheThresholdAsksAndMarksOnlyWhenConfirmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixChangeThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixChangeTest;
	FAffixChangeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float Threshold =
		UCataclysmEquipmentComponent::ConsumptionThresholdFor(ACataclysmGameMode::DifficultyTierIn(Scene.World));
	// A HELM WORN 10 BELOW THE THRESHOLD: a tier 1 addition's 5 stays below it, then a shift's 15 crosses it.
	const ECataclysmGearSlot Head = Wear(Scene, Helm(Threshold - 10.0f - Scene.Worn()->WornResidue()));
	if (!TestTrue(TEXT("set-up: the helm is worn"), Scene.Worn()->EquippedAt(Head) != nullptr))
	{
		return false;
	}
	Scene.Bag()->AddMaterial(Fragments, 1);
	Scene.Bag()->AddMaterial(Crystal, 1);

	FCataclysmForgeAffixChange Done;
	FString Message;
	const ECataclysmForgeResult Below = UCataclysmForgeCraft::AddAffix(Scene.World, FCataclysmForgeTarget::Worn(Head),
																	   BlockChance, 1, false, Done, Message);
	TestEqual(TEXT("CONTROL: a tier 1 addition that stays below is done at once"), static_cast<int32>(Below),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestFalse(TEXT("CONTROL: and does not mark the character"), Scene.State->IsMarkedByConsumption());

	const int32 DayBefore = Scene.Run->Day();
	const FName RowBefore = Scene.Worn()->EquippedAt(Head)->Affixes[0].Affix;
	const ECataclysmForgeResult Asked =
		UCataclysmForgeCraft::ShiftAffix(Scene.World, FCataclysmForgeTarget::Worn(Head), 0, false, Done, Message);
	TestEqual(TEXT("shifting affix 1 asks first"), static_cast<int32>(Asked),
			  static_cast<int32>(ECataclysmForgeResult::AsksAboutTheThreshold));
	TestTrue(TEXT("saying what the craft is and naming the threshold"),
			 Message.StartsWith(TEXT("Shifting an affix of ")) && Message.Contains(TEXT("Consumption Threshold")));
	TestTrue(TEXT("asking changes nothing: affix 1 is as it was"), Scene.Worn()->EquippedAt(Head)->Affixes[0].Affix == RowBefore);
	TestEqual(TEXT("nor the crystal"), Scene.Bag()->CountOfMaterial(Crystal), 1);
	TestEqual(TEXT("nor the day"), Scene.Run->Day(), DayBefore);
	TestFalse(TEXT("nor marks the character"), Scene.State->IsMarkedByConsumption());

	const ECataclysmForgeResult Confirmed =
		UCataclysmForgeCraft::ShiftAffix(Scene.World, FCataclysmForgeTarget::Worn(Head), 0, true, Done, Message);
	AddInfo(FString::Printf(TEXT("CHANGE THRESHOLD: %.0f; worn %.0f after; confirmed %d, marked %s"), Threshold,
							Scene.Worn()->WornResidue(), static_cast<int32>(Confirmed),
							Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("confirmed, it is done"), static_cast<int32>(Confirmed), static_cast<int32>(ECataclysmForgeResult::Done));
	TestTrue(TEXT("affix 1 is another affix"), Scene.Worn()->EquippedAt(Head)->Affixes[0].Affix != RowBefore);
	TestTrue(TEXT("the Worn Residue is past the threshold"), Scene.Worn()->WornResidue() >= Threshold);
	TestTrue(TEXT("the character is marked"), Scene.State->IsMarkedByConsumption());
	TestTrue(TEXT("and the build its double wears is held"), Scene.State->HoldsConsumptionBuild());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
