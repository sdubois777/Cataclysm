// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Interface/CataclysmForgeWidget.h"
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
 * THE FORGE'S THREE DAMAGE-TYPE CRAFTS. Issue #47, slice F4, ruled by the coordinating session on 2026-11-22.
 *
 * The Crafting sheet's rows: Infuse Random Damage Type, an Elemental Cinder, 10 residue, 1 base day, Base Gold 250;
 * Infuse Specific Damage Type, a Sovereign Ingot, 50 residue, 5 base days, Base Gold 1,250; Change Damage Type, a
 * Prismatic Catalyst, 20 residue, 0 base days, Base Gold 250. Weapons only; the two infusions refused at the drop's
 * maximum, a change working at it; a drawn type drawn among those designed for the weapon's type that it lacks, from the
 * item, the run's mask and the day; the weapon made at creation refused (issue #2845). The scene is
 * `CataclysmForgeAffixesTests.cpp`'s, copied into this file's namespace because the module is built in unity files.
 * The fixed figures were computed with the Python copy in `tools/tests/test_the_forge_changes_damage_types.py`.
 */
namespace CataclysmForgeDamageTypesTest
{
	const FName Cinder(UCataclysmForge::ElementalCinderRow);
	const FName Ingot(UCataclysmForge::SovereignIngotRow);
	const FName Catalyst(UCataclysmForge::PrismaticCatalystRow);
	const FName War(TEXT("War"));
	const FName Death(TEXT("Death"));
	const FName Demonic(TEXT("Demonic"));
	const FName Void(TEXT("Void"));

	/** THE FIXED SWORD: one-handed, gear level 3, War and Death of the seven designed for a sword, residue 40. */
	FCataclysmItem Sword(float Residue = 40.0f, const TArray<FName>& Types = {FName(TEXT("War")), FName(TEXT("Death"))})
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Weapon_Sword"));
		Item.GearLevel = 3;
		Item.DamageTypes = Types;
		Item.Residue = Residue;
		return Item;
	}

	/** A shield holding all three types designed for a shield, War, Celestial and Chaos: its maximum, 3. */
	FCataclysmItem FullShield()
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Weapon_Shield"));
		Item.DamageTypes = {FName(TEXT("War")), FName(TEXT("Celestial")), FName(TEXT("Chaos"))};
		Item.Residue = 40.0f;
		return Item;
	}

	struct FDamageTypeCraftScene
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
	FDamageTypeCraftScene MakeScene()
	{
		FDamageTypeCraftScene Out;
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
			Out.State->SetIdentity(TEXT("Infuser"), Partition);
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

	ECataclysmGearSlot Wear(const FDamageTypeCraftScene& Scene, const FCataclysmItem& Item)
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Scene.Worn()->Equip(Item, First, Second, Went);
		return Went;
	}

	FString Labels(const TArray<FCataclysmForgeDamageTypeAction>& Actions)
	{
		TArray<FString> Out;
		for (const FCataclysmForgeDamageTypeAction& Action : Actions)
		{
			Out.Add(Action.Label);
		}
		return FString::Join(Out, TEXT(" | "));
	}

	FString TypesOf(const FCataclysmItem& Item)
	{
		return FString::JoinBy(Item.DamageTypes, TEXT(", "), [](const FName& Type) { return Type.ToString(); });
	}

	/** The plan the craft will make for this weapon now, so a test can compare with what it did. */
	FCataclysmForgeDamageTypeWork PlanNow(const FDamageTypeCraftScene& Scene, const FCataclysmItem& Item, ECataclysmForgeDamageTypeCraft Craft,
										 FName Chosen)
	{
		const bool bChange = Craft == ECataclysmForgeDamageTypeCraft::Change;
		const int32 Seed = UCataclysmForge::AffixCraftSeed(
			Item, bChange ? UCataclysmForge::ChangedDamageTypeSalt : UCataclysmForge::InfusedDamageTypeSalt,
			bChange ? Item.DamageTypes.IndexOfByKey(Chosen) : Item.DamageTypes.Num(), Scene.Run->WalkSeedMask,
			Scene.Run->Day());
		return UCataclysmForge::DamageTypeWorkOf(Item, Craft, Chosen, Seed, UCataclysmWeaponSkills::LoadGeneratedTable(),
												UCataclysmItemModifiers::LoadBaseTable(), 1, 1, 1000000);
	}
}

// --------------------------------------------------------------------------------------------------------------------
// FD1. A random infusion: the rule's figures and question, and the craft on a carried sword.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeDamageTypesRandomTest,
	"Cataclysm.ForgeDamageTypes.InfusingARandomDamageTypeTakesAnElementalCinderTenResidueADayAndDrawsOneTheWeaponLacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeDamageTypesRandomTest::RunTest(const FString&)
{
	using namespace CataclysmForgeDamageTypesTest;
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table"), Skills) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	constexpr ECataclysmForgeDamageTypeCraft Random = ECataclysmForgeDamageTypeCraft::InfuseRandom;
	const FCataclysmItem Item = Sword();

	const FCataclysmForgeDamageTypeWork Plan =
		UCataclysmForge::DamageTypeWorkOf(Item, Random, NAME_None, 688249076, Skills, Bases, 1, 1, 1000000);
	TestTrue(TEXT("a sword with 2 of its 4 can take another"), Plan.bPossible);
	TestEqual(TEXT("with an Elemental Cinder"), Plan.Material, Cinder);
	TestEqual(TEXT("a sword's maximum is 4"), Plan.MaxTypes, 4);
	TestEqual(TEXT("residue 40 becomes 50"), Plan.ResidueAfter, 50.0f, 0.001f);
	TestEqual(TEXT("one day below 100 residue"), Plan.Days, 1);
	TestEqual(TEXT("250 gold, at 40 residue 450"), Plan.Gold, static_cast<int64>(450));
	TestTrue(TEXT("seed 688249076 draws Chaos, as computed"), Plan.ToType == FName(TEXT("Chaos")));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::DamageTypeQuestion(Plan),
			  FString(TEXT("Press again to infuse a random damage type: one Elemental Cinder, 450 gold, residue 40 to 50, 1 "
						   "day of the run; the type is drawn among those the weapon can take.")));
	TestEqual(TEXT("no Elemental Cinder"),
			  UCataclysmForge::DamageTypeWorkOf(Item, Random, NAME_None, 1, Skills, Bases, 0, 1, 1000000).Refusal,
			  FString(TEXT("Infusing a random damage type takes an Elemental Cinder; 0 held.")));
	TestEqual(TEXT("too little gold"),
			  UCataclysmForge::DamageTypeWorkOf(Item, Random, NAME_None, 1, Skills, Bases, 1, 1, 100).Refusal,
			  FString(TEXT("Infusing a random damage type takes 450 gold; 100 gold held.")));

	FDamageTypeCraftScene Scene = MakeScene();
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
	Scene.Bag()->AddMaterial(Cinder, 1);
	const FName Expected = PlanNow(Scene, *Scene.Bag()->ItemAt(Cell), Random, NAME_None).ToType;
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	FCataclysmForgeDamageTypeWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftDamageType(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), Random, NAME_None, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the infusion is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the sword is still in its cell"), After))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("INFUSED: %s (expected %s last), residue %.0f, day %d to %d, gold %lld"), *TypesOf(*After),
							*Expected.ToString(), After->Residue, DayBefore, Scene.Run->Day(),
							GoldBefore - Scene.State->GetOwnGold()));
	TestTrue(TEXT("War, Death and the type drawn for this sword, run and day, last"),
			 After->DamageTypes.Num() == 3 && After->DamageTypes[0] == War && After->DamageTypes[1] == Death
				 && After->DamageTypes[2] == Expected);
	TestTrue(TEXT("a type it lacked"), Expected != War && Expected != Death && !Expected.IsNone());
	TestEqual(TEXT("residue 40 becomes 50"), After->Residue, 50.0f, 0.001f);
	TestEqual(TEXT("the Elemental Cinder is used"), Scene.Bag()->CountOfMaterial(Cinder), 0);
	TestEqual(TEXT("one day passed"), Scene.Run->Day(), DayBefore + 1);
	TestEqual(TEXT("450 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(450));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FD2. The draw: the fixed sword's seed text and seeds, the types drawn, the same again, and an even spread over the
// five it lacks. Every figure computed in Python.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeDamageTypesDrawTest,
	"Cataclysm.ForgeDamageTypes.ADrawnTypeIsEvenAmongThoseTheWeaponLacksAndTheSameForTheSameRunDayAndWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeDamageTypesDrawTest::RunTest(const FString&)
{
	using namespace CataclysmForgeDamageTypesTest;
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table"), Skills) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	const FCataclysmItem Item = Sword();
	TestEqual(TEXT("the sword's seed text, as the Python copy writes it"), UCataclysmForge::ItemSeedText(Item),
			  FString(TEXT("Base=Weapon_Sword;Level=3;Types=War+Death;Affixes=;Enchantments=0:;Sockets=;Residue=40.000000;"
						   "Kills=0;Made=0")));
	TestEqual(TEXT("the five types it lacks of the seven designed for a sword"),
			  FString::JoinBy(UCataclysmForge::DamageTypesOpenTo(Item, Skills, Bases), TEXT(", "),
							  [](const FName& Type) { return Type.ToString(); }),
			  FString(TEXT("Demonic, Pestilence, Famine, Celestial, Chaos")));
	const int32 InfuseSeed = UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::InfusedDamageTypeSalt, 2, 12345, 7);
	const int32 ChangeSeed = UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::ChangedDamageTypeSalt, 0, 12345, 7);
	TestEqual(TEXT("a random infusion's seed on day 7 of mask 12345"), InfuseSeed, 688249076);
	TestEqual(TEXT("a change of War's"), ChangeSeed, 719900450);
	const auto Drawn = [&](ECataclysmForgeDamageTypeCraft Craft, FName Chosen, int32 Seed)
	{
		return UCataclysmForge::DamageTypeWorkOf(Item, Craft, Chosen, Seed, Skills, Bases, 1, 1, 1000000).ToType;
	};
	TestTrue(TEXT("the infusion draws Chaos"), Drawn(ECataclysmForgeDamageTypeCraft::InfuseRandom, NAME_None, InfuseSeed)
												   == FName(TEXT("Chaos")));
	TestTrue(TEXT("and Chaos again"), Drawn(ECataclysmForgeDamageTypeCraft::InfuseRandom, NAME_None, InfuseSeed)
										  == FName(TEXT("Chaos")));
	TestTrue(TEXT("the change of War draws Famine"),
			 Drawn(ECataclysmForgeDamageTypeCraft::Change, War, ChangeSeed) == FName(TEXT("Famine")));

	TMap<FName, int32> Counts;
	for (int32 Each = 1; Each <= 3000; ++Each)
	{
		++Counts.FindOrAdd(Drawn(ECataclysmForgeDamageTypeCraft::InfuseRandom, NAME_None,
								 UCataclysmForge::AffixCraftSeed(Item, UCataclysmForge::InfusedDamageTypeSalt, 2, 12345, Each)));
	}
	AddInfo(FString::Printf(TEXT("TYPE DRAWS over days 1 to 3000: Demonic %d, Pestilence %d, Famine %d, Celestial %d, Chaos %d; "
								 "others %d"),
							Counts.FindRef(Demonic), Counts.FindRef(FName(TEXT("Pestilence"))),
							Counts.FindRef(FName(TEXT("Famine"))), Counts.FindRef(FName(TEXT("Celestial"))),
							Counts.FindRef(FName(TEXT("Chaos"))), 3000 - Counts.FindRef(Demonic)
								- Counts.FindRef(FName(TEXT("Pestilence"))) - Counts.FindRef(FName(TEXT("Famine")))
								- Counts.FindRef(FName(TEXT("Celestial"))) - Counts.FindRef(FName(TEXT("Chaos")))));
	TestEqual(TEXT("Demonic 592, as computed"), Counts.FindRef(Demonic), 592);
	TestEqual(TEXT("Pestilence 605"), Counts.FindRef(FName(TEXT("Pestilence"))), 605);
	TestEqual(TEXT("Famine 601"), Counts.FindRef(FName(TEXT("Famine"))), 601);
	TestEqual(TEXT("Celestial 585"), Counts.FindRef(FName(TEXT("Celestial"))), 585);
	TestEqual(TEXT("Chaos 617"), Counts.FindRef(FName(TEXT("Chaos"))), 617);
	TestEqual(TEXT("never War, Death or Void"), Counts.FindRef(War) + Counts.FindRef(Death) + Counts.FindRef(Void), 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FD3. A specific infusion: the rule's figures and question, its refusals and the maximum, and the craft.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeDamageTypesSpecificTest,
	"Cataclysm.ForgeDamageTypes.InfusingAChosenDamageTypeTakesASovereignIngotFiftyResidueFiveDaysUpToTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeDamageTypesSpecificTest::RunTest(const FString&)
{
	using namespace CataclysmForgeDamageTypesTest;
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table"), Skills) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	constexpr ECataclysmForgeDamageTypeCraft Specific = ECataclysmForgeDamageTypeCraft::InfuseSpecific;
	const FCataclysmItem Item = Sword();
	const auto Refusal = [&](const FCataclysmItem& On, FName Chosen)
	{
		return UCataclysmForge::DamageTypeWorkOf(On, Specific, Chosen, 1, Skills, Bases, 1, 1, 1000000).Refusal;
	};

	const FCataclysmForgeDamageTypeWork Plan =
		UCataclysmForge::DamageTypeWorkOf(Item, Specific, Demonic, 1, Skills, Bases, 1, 1, 1000000);
	TestTrue(TEXT("Demonic can be infused into the sword"), Plan.bPossible && Plan.ToType == Demonic);
	TestEqual(TEXT("with a Sovereign Ingot"), Plan.Material, Ingot);
	TestEqual(TEXT("residue 40 becomes 90"), Plan.ResidueAfter, 90.0f, 0.001f);
	TestEqual(TEXT("five days below 100 residue"), Plan.Days, 5);
	TestEqual(TEXT("1,250 gold, at 40 residue 2,250"), Plan.Gold, static_cast<int64>(2250));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::DamageTypeQuestion(Plan),
			  FString(TEXT("Press again to infuse Demonic: one Sovereign Ingot, 2,250 gold, residue 40 to 90, 5 days of the "
						   "run.")));
	TestEqual(TEXT("a type no sword can carry"), Refusal(Item, Void), FString(TEXT("That weapon cannot carry Void.")));
	TestEqual(TEXT("a type it carries"), Refusal(Item, War), FString(TEXT("That weapon carries War already.")));
	TestEqual(TEXT("a sword at its 4"), Refusal(Sword(40.0f, {War, Death, Demonic, FName(TEXT("Chaos"))}), FName(TEXT("Famine"))),
			  FString(TEXT("That weapon carries its most damage types, 4.")));
	TestEqual(TEXT("a shield at its 3, all a shield can carry"), Refusal(FullShield(), Void),
			  FString(TEXT("That weapon carries its most damage types, 3.")));
	TestEqual(TEXT("no Sovereign Ingot"),
			  UCataclysmForge::DamageTypeWorkOf(Item, Specific, Demonic, 1, Skills, Bases, 0, 1, 1000000).Refusal,
			  FString(TEXT("Infusing a chosen damage type takes a Sovereign Ingot; 0 held.")));

	FDamageTypeCraftScene Scene = MakeScene();
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
	Scene.Bag()->AddMaterial(Ingot, 1);
	const int32 DayBefore = Scene.Run->Day();
	FCataclysmForgeDamageTypeWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftDamageType(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), Specific, Demonic, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the infusion is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the sword is still in its cell"), After))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("INFUSED DEMONIC: %s, residue %.0f, day %d to %d"), *TypesOf(*After), After->Residue,
							DayBefore, Scene.Run->Day()));
	TestEqual(TEXT("War, Death, Demonic"), TypesOf(*After), FString(TEXT("War, Death, Demonic")));
	TestEqual(TEXT("residue 40 becomes 90"), After->Residue, 90.0f, 0.001f);
	TestEqual(TEXT("the Sovereign Ingot is used"), Scene.Bag()->CountOfMaterial(Ingot), 0);
	TestEqual(TEXT("five days passed"), Scene.Run->Day(), DayBefore + 5);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FD4. A change: the rule's figures and question, its refusals, that it works at the maximum, and the craft.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeDamageTypesChangeTest,
	"Cataclysm.ForgeDamageTypes.ChangingADamageTypeTakesAPrismaticCatalystTwentyResidueAndWorksAtTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeDamageTypesChangeTest::RunTest(const FString&)
{
	using namespace CataclysmForgeDamageTypesTest;
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table"), Skills) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	constexpr ECataclysmForgeDamageTypeCraft Change = ECataclysmForgeDamageTypeCraft::Change;
	const FCataclysmItem Item = Sword();

	const FCataclysmForgeDamageTypeWork Plan =
		UCataclysmForge::DamageTypeWorkOf(Item, Change, War, 719900450, Skills, Bases, 1, 1, 1000000);
	TestTrue(TEXT("War can be changed"), Plan.bPossible && Plan.FromType == War);
	TestEqual(TEXT("with a Prismatic Catalyst"), Plan.Material, Catalyst);
	TestTrue(TEXT("seed 719900450 draws Famine"), Plan.ToType == FName(TEXT("Famine")));
	TestEqual(TEXT("residue 40 becomes 60"), Plan.ResidueAfter, 60.0f, 0.001f);
	TestEqual(TEXT("no day below 100 residue"), Plan.Days, 0);
	TestEqual(TEXT("250 gold, at 40 residue 450"), Plan.Gold, static_cast<int64>(450));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::DamageTypeQuestion(Plan),
			  FString(TEXT("Press again to change War to another damage type: one Prismatic Catalyst, 450 gold, residue 40 "
						   "to 60, 0 days of the run; the new type is drawn among those the weapon lacks.")));
	TestTrue(TEXT("a sword at its 4 can still change one"),
			 UCataclysmForge::DamageTypeWorkOf(Sword(40.0f, {War, Death, Demonic, FName(TEXT("Chaos"))}), Change, War, 1, Skills,
											   Bases, 1, 1, 1000000).bPossible);
	TestEqual(TEXT("a shield holding every type designed for it"),
			  UCataclysmForge::DamageTypeWorkOf(FullShield(), Change, War, 1, Skills, Bases, 1, 1, 1000000).Refusal,
			  FString(TEXT("That weapon carries every damage type designed for it.")));
	TestEqual(TEXT("a type it does not carry"),
			  UCataclysmForge::DamageTypeWorkOf(Item, Change, Void, 1, Skills, Bases, 1, 1, 1000000).Refusal,
			  FString(TEXT("That weapon does not carry Void.")));
	TestEqual(TEXT("no Prismatic Catalyst"),
			  UCataclysmForge::DamageTypeWorkOf(Item, Change, War, 1, Skills, Bases, 0, 1, 1000000).Refusal,
			  FString(TEXT("Changing a damage type takes a Prismatic Catalyst; 0 held.")));

	FDamageTypeCraftScene Scene = MakeScene();
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
	Scene.Bag()->AddMaterial(Catalyst, 1);
	const FName Expected = PlanNow(Scene, *Scene.Bag()->ItemAt(Cell), Change, War).ToType;
	const int32 DayBefore = Scene.Run->Day();
	FCataclysmForgeDamageTypeWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftDamageType(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), Change, War, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the change is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the sword is still in its cell"), After))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("CHANGED WAR: %s (expected %s first), residue %.0f, day %d to %d"), *TypesOf(*After),
							*Expected.ToString(), After->Residue, DayBefore, Scene.Run->Day()));
	TestTrue(TEXT("War is replaced in its place by the type drawn for this sword, run and day; Death kept"),
			 After->DamageTypes.Num() == 2 && After->DamageTypes[0] == Expected && After->DamageTypes[1] == Death);
	TestTrue(TEXT("a type it lacked"), Expected != War && Expected != Death && !Expected.IsNone());
	TestEqual(TEXT("residue 40 becomes 60"), After->Residue, 60.0f, 0.001f);
	TestEqual(TEXT("the Prismatic Catalyst is used"), Scene.Bag()->CountOfMaterial(Catalyst), 0);
	TestEqual(TEXT("no day passed"), Scene.Run->Day(), DayBefore);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FD5. Weapons only, never the weapon made at creation (issue #2845): the rule's refusals, the screen's buttons and
// detail, and two presses on the screen infusing a type.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeDamageTypesScreenTest,
	"Cataclysm.ForgeDamageTypes.OnlyAWeaponNotTheOneMadeAtCreationIsOfferedTheCrafts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeDamageTypesScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeDamageTypesTest;
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table"), Skills) || !TestNotNull(TEXT("set-up: the base table"), Bases))
	{
		return false;
	}
	FCataclysmItem Helm;
	Helm.Base = FName(TEXT("Head_Helm"));
	FCataclysmItem Creation = Sword();
	Creation.bMadeAtCreation = true;
	const FCataclysmItem Full = Sword(40.0f, {War, Death, Demonic, FName(TEXT("Chaos"))});
	for (const ECataclysmForgeDamageTypeCraft Craft : {ECataclysmForgeDamageTypeCraft::InfuseRandom,
													   ECataclysmForgeDamageTypeCraft::InfuseSpecific,
													   ECataclysmForgeDamageTypeCraft::Change})
	{
		TestEqual(TEXT("a helm, each craft"),
				  UCataclysmForge::DamageTypeWorkOf(Helm, Craft, War, 1, Skills, Bases, 1, 1, 1000000).Refusal,
				  FString(TEXT("Only a weapon carries damage types.")));
		TestEqual(TEXT("the weapon made at creation, each craft"),
				  UCataclysmForge::DamageTypeWorkOf(Creation, Craft, War, 1, Skills, Bases, 1, 1, 1000000).Refusal,
				  FString(UCataclysmForge::CreationWeaponRefusal));
	}

	const auto Offered = [&](const FCataclysmItem& Item)
	{
		return Labels(UCataclysmForgeLayout::DamageTypeActionsFor(
			Item, UCataclysmForge::DamageTypesOpenTo(Item, Skills, Bases), UCataclysmForge::MaxDamageTypesOf(Item, Skills, Bases)));
	};
	AddInfo(FString::Printf(TEXT("DAMAGE TYPE BUTTONS: sword: %s; at its 4: %s; creation: '%s'; helm: '%s'"), *Offered(Sword()),
							*Offered(Full), *Offered(Creation), *Offered(Helm)));
	TestEqual(TEXT("the sword: a random infusion, each type it lacks, and a change of each it carries"), Offered(Sword()),
			  FString(TEXT("Infuse a random damage type | Infuse Demonic | Infuse Pestilence | Infuse Famine | Infuse "
						   "Celestial | Infuse Chaos | Change War | Change Death")));
	TestEqual(TEXT("at its 4: changes only"), Offered(Full),
			  FString(TEXT("Change War | Change Death | Change Demonic | Change Chaos")));
	TestEqual(TEXT("the weapon made at creation: none"), Offered(Creation), FString());
	TestEqual(TEXT("a helm: none"), Offered(Helm), FString());
	TestEqual(TEXT("the sword's detail line"), UCataclysmForgeLayout::DamageTypesDetailFor(Sword(), 4),
			  FString(TEXT("Damage types: War, Death; 2 of at most 4.")));
	TestTrue(TEXT("the creation weapon's says why"),
			 UCataclysmForgeLayout::DamageTypesDetailFor(Creation, 4).EndsWith(UCataclysmForge::CreationWeaponRefusal));
	TestEqual(TEXT("a helm's is empty"), UCataclysmForgeLayout::DamageTypesDetailFor(Helm, 0), FString());

	FDamageTypeCraftScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Sword());
	Scene.Bag()->AddMaterial(Ingot, 1);
	UCataclysmForgeWidget* Screen = NewObject<UCataclysmForgeWidget>();
	if (!TestNotNull(TEXT("set-up: the screen was made"), Screen))
	{
		return false;
	}
	Screen->SetWorldContextForTests(Scene.World);
	Screen->RefreshDisplay();
	Screen->ClickPieceForTests(UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Cell)));
	TestEqual(TEXT("the chosen sword shows eight damage-type buttons"), Screen->DamageTypeActionsShown().Num(), 8);
	TestTrue(TEXT("and its types in the detail"), Screen->DetailText().Contains(TEXT("Damage types: War, Death; 2 of at most 4.")));
	const FName InfuseCelestial(TEXT("InfuseType_Celestial"));
	Screen->ClickDamageTypeActionForTests(InfuseCelestial);
	TestTrue(TEXT("the first press asks"), Screen->MessageText().StartsWith(TEXT("Press again to infuse Celestial")));
	TestEqual(TEXT("and changes nothing"), Scene.Bag()->ItemAt(Cell)->DamageTypes.Num(), 2);
	Screen->ClickDamageTypeActionForTests(InfuseCelestial);
	AddInfo(FString::Printf(TEXT("SCREEN INFUSED: %s; %s"), *TypesOf(*Scene.Bag()->ItemAt(Cell)), *Screen->MessageText()));
	TestEqual(TEXT("the second press does it"), TypesOf(*Scene.Bag()->ItemAt(Cell)), FString(TEXT("War, Death, Celestial")));
	TestTrue(TEXT("and says so"), Screen->MessageText().StartsWith(TEXT("The weapon carries Celestial now.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FD6. A worn weapon: a craft that stays below the Consumption Threshold is done at once; one that would cross it
// asks, changes nothing, and marks the character only when confirmed.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeDamageTypesThresholdTest,
	"Cataclysm.ForgeDamageTypes.AWornWeaponsCraftThatWouldCrossTheThresholdAsksAndMarksOnlyWhenConfirmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeDamageTypesThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmForgeDamageTypesTest;
	FDamageTypeCraftScene Scene = MakeScene();
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
	// A SWORD WORN 60 BELOW THE THRESHOLD: infusing Celestial's 50 stays below it, then infusing Demonic's 50 takes it past.
	// Both chosen, so no draw can make the second ask for a type the first added.
	const ECataclysmGearSlot Hand = Wear(Scene, Sword(Threshold - 60.0f - Scene.Worn()->WornResidue()));
	if (!TestTrue(TEXT("set-up: the sword is worn"), Scene.Worn()->EquippedAt(Hand) != nullptr))
	{
		return false;
	}
	Scene.Bag()->AddMaterial(Ingot, 2);

	FCataclysmForgeDamageTypeWork Done;
	FString Message;
	const ECataclysmForgeResult Below = UCataclysmForgeCraft::CraftDamageType(
		Scene.World, FCataclysmForgeTarget::Worn(Hand), ECataclysmForgeDamageTypeCraft::InfuseSpecific,
		FName(TEXT("Celestial")), false, Done, Message);
	TestEqual(TEXT("CONTROL: infusing Celestial, which stays below, is done at once"), static_cast<int32>(Below),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestFalse(TEXT("CONTROL: and does not mark the character"), Scene.State->IsMarkedByConsumption());

	const int32 TypesBefore = Scene.Worn()->EquippedAt(Hand)->DamageTypes.Num();
	const int32 DayBefore = Scene.Run->Day();
	const ECataclysmForgeResult Asked = UCataclysmForgeCraft::CraftDamageType(
		Scene.World, FCataclysmForgeTarget::Worn(Hand), ECataclysmForgeDamageTypeCraft::InfuseSpecific, Demonic, false, Done,
		Message);
	TestEqual(TEXT("infusing Demonic asks first"), static_cast<int32>(Asked),
			  static_cast<int32>(ECataclysmForgeResult::AsksAboutTheThreshold));
	TestTrue(TEXT("saying what the craft is and naming the threshold"),
			 Message.StartsWith(TEXT("Infusing a damage type into ")) && Message.Contains(TEXT("Consumption Threshold")));
	TestEqual(TEXT("asking changes nothing: the types"), Scene.Worn()->EquippedAt(Hand)->DamageTypes.Num(), TypesBefore);
	TestEqual(TEXT("nor the ingot"), Scene.Bag()->CountOfMaterial(Ingot), 1);
	TestEqual(TEXT("nor the day"), Scene.Run->Day(), DayBefore);
	TestFalse(TEXT("nor marks the character"), Scene.State->IsMarkedByConsumption());

	const ECataclysmForgeResult Confirmed = UCataclysmForgeCraft::CraftDamageType(
		Scene.World, FCataclysmForgeTarget::Worn(Hand), ECataclysmForgeDamageTypeCraft::InfuseSpecific, Demonic, true, Done,
		Message);
	AddInfo(FString::Printf(TEXT("TYPE THRESHOLD: %.0f; worn %.0f after; types %s; confirmed %d, marked %s"), Threshold,
							Scene.Worn()->WornResidue(), *TypesOf(*Scene.Worn()->EquippedAt(Hand)),
							static_cast<int32>(Confirmed), Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("confirmed, it is done"), static_cast<int32>(Confirmed), static_cast<int32>(ECataclysmForgeResult::Done));
	TestTrue(TEXT("the sword carries Demonic"), Scene.Worn()->EquippedAt(Hand)->DamageTypes.Contains(Demonic));
	TestTrue(TEXT("the Worn Residue is past the threshold"), Scene.Worn()->WornResidue() >= Threshold);
	TestTrue(TEXT("the character is marked"), Scene.State->IsMarkedByConsumption());
	TestTrue(TEXT("and the build its double wears is held"), Scene.State->HoldsConsumptionBuild());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
