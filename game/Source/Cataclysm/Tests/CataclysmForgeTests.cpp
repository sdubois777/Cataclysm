// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Cataclysmic Forge's upgrade and combine: the owner's ruling of 2026-11-19, "build it next", and the coordinating
 * session's rulings a to i of the same day. `UCataclysmForge` holds the rules and `UCataclysmForgeCraft` does them to
 * the character playing; the screen's tests are in `CataclysmForgeScreenTests.cpp`.
 *
 * THE ITEM BASES ARE REAL ROWS OF `game/Data/ItemBases.csv` and the stones real rows of `CraftingMaterials.csv`,
 * because `Equip` looks a base up to choose its slot and the forge names stones by those rows.
 */
namespace CataclysmForgeTest
{
	const TCHAR* GreataxeBase = TEXT("Weapon_Greataxe");
	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* GlovesBase = TEXT("Gloves_Handwraps");
	const TCHAR* BootsBase = TEXT("Boots_Treads");

	FCataclysmItem PlainOf(const TCHAR* Base, int32 GearLevel, float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		Item.Residue = Residue;
		return Item;
	}

	FName Stone(int32 Level)
	{
		return UCataclysmForge::StoneFor(Level);
	}

	/** A world with this project's game instance, a possessed player, and a run on the instance; nothing carried. */
	struct FForgeScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && State && Character && Character->GetInventory()
				&& Character->GetEquipment() && Run && Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }
		UCataclysmEquipmentComponent* Worn() const { return Character->GetEquipment(); }
	};

	void WaitForTheSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}

	void DeleteSlot(const FString& SlotName)
	{
		WaitForTheSlot(SlotName);
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UCataclysmSaveWriter::UserIndex);
		}
	}

	/** The scene, emptied: no item carried and nothing worn, so each test puts on exactly what it reads. */
	FForgeScene MakeScene(bool bWriting = false)
	{
		FForgeScene Out;
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
			Out.State->SetIdentity(TEXT("Smith"), Partition);
			Out.State->SetOwnGold(1000000);
		}

		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2766, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;

		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}

		if (bWriting)
		{
			if (UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(Out.World))
			{
				Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
			}
		}
		return Out;
	}

	/** Wears an item through `Equip`, and answers where it went. */
	ECataclysmGearSlot Wear(const FForgeScene& Scene, const FCataclysmItem& Item)
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Scene.Worn()->Equip(Item, First, Second, Went);
		return Went;
	}
}

// ---------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------

/**
 * Raising a piece one level takes two stones of the level it goes to, adds 25 residue, and costs one day and the
 * residue's time penalty, `CR / 100` rounded down, read before the upgrade adds its own. Refused at +10, with too few
 * stones, and for no item. Prints "UPGRADE RULES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeUpgradeRulesTest,
	"Cataclysm.Forge.AnUpgradeTakesTwoStonesOfTheNextLevel25ResidueAndADayPerHundred",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeUpgradeRulesTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	const FCataclysmForgeUpgrade Plan = UCataclysmForge::UpgradeOf(PlainOf(GlovesBase, 2, 87.0f), 5, 1, 1000000);
	TestTrue(TEXT("a +2 piece with five +3 stones can be raised"), Plan.bPossible);
	TestEqual(TEXT("to +3"), Plan.ToLevel, 3);
	TestEqual(TEXT("with the +3 stone"), Plan.Stone, FName(TEXT("Material_Upgrade_Stone_3")));
	TestEqual(TEXT("two of them"), Plan.StonesNeeded, 2);
	TestEqual(TEXT("residue 87 becomes 112"), Plan.ResidueAfter, 112.0f, 0.001f);
	TestEqual(TEXT("three days: the level it reaches, and none for residue below 100 (#2830)"), Plan.Days, 3);
	TestEqual(TEXT("and 2,055 gold: 750 for three days, times 87 / 50 + 1 (#2830)"), Plan.Gold, static_cast<int64>(2055));

	TestEqual(TEXT("99.9 residue: one day"), UCataclysmForge::DaysFor(99.9f, 1), 1);
	TestEqual(TEXT("100 residue: two days, the break point"), UCataclysmForge::DaysFor(100.0f, 1), 2);
	TestEqual(TEXT("250 residue: three days"), UCataclysmForge::DaysFor(250.0f, 1), 3);
	TestEqual(TEXT("no residue: one day"), UCataclysmForge::DaysFor(0.0f, 1), 1);

	const FCataclysmForgeUpgrade Short = UCataclysmForge::UpgradeOf(PlainOf(GlovesBase, 2, 87.0f), 1, 1, 1000000);
	TestFalse(TEXT("one +3 stone is not enough"), Short.bPossible);
	TestEqual(TEXT("and it says so"), Short.Refusal, FString(TEXT("Raising it to +3 takes two +3 upgrade stones; 1 held.")));

	const FCataclysmForgeUpgrade Top = UCataclysmForge::UpgradeOf(PlainOf(GlovesBase, 10, 300.0f), 99, 1, 1000000);
	TestFalse(TEXT("+10 is the highest"), Top.bPossible);
	TestEqual(TEXT("and it says so"), Top.Refusal, FString(TEXT("This is +10, the highest an item can be raised to.")));
	TestTrue(TEXT("and names no stone"), Top.Stone.IsNone());

	TestFalse(TEXT("no item is no upgrade"), UCataclysmForge::UpgradeOf(FCataclysmItem(), 9, 1, 1000000).bPossible);
	TestEqual(TEXT("the +1 stone's row"), UCataclysmForge::StoneFor(1), FName(TEXT("Material_Upgrade_Stone_1")));
	TestTrue(TEXT("no +11 stone"), UCataclysmForge::StoneFor(11).IsNone());

	AddInfo(FString::Printf(TEXT("UPGRADE RULES: %s"), *UCataclysmForge::UpgradeQuestion(Plan)));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::UpgradeQuestion(Plan),
			  FString(TEXT("Press again to upgrade to +3: two +3 stones, 2,055 gold, residue 87 to 112, 3 days of the run.")));
	return true;
}

/** Two stones of a level make one of the next; +10 is the highest, and one stone is not two. Prints "COMBINE RULES". */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeCombineRulesTest,
	"Cataclysm.Forge.CombiningTakesTwoStonesOfALevelAndMakesOneOfTheNext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeCombineRulesTest::RunTest(const FString&)
{
	const FCataclysmForgeCombine Two = UCataclysmForge::CombineOf(2, 3, 1, 1000000);
	TestTrue(TEXT("three +2 stones can make a +3"), Two.bPossible);
	TestEqual(TEXT("from the +2 row"), Two.From, FName(TEXT("Material_Upgrade_Stone_2")));
	TestEqual(TEXT("into the +3 row"), Two.Into, FName(TEXT("Material_Upgrade_Stone_3")));
	TestEqual(TEXT("taking two"), Two.StonesNeeded, 2);

	const FCataclysmForgeCombine One = UCataclysmForge::CombineOf(2, 1, 1, 1000000);
	TestFalse(TEXT("one +2 stone is not two"), One.bPossible);
	TestEqual(TEXT("and it says so"), One.Refusal, FString(TEXT("Combining takes two +2 upgrade stones; 1 held.")));

	TestFalse(TEXT("+10 is the highest stone"), UCataclysmForge::CombineOf(10, 9, 1, 1000000).bPossible);
	TestFalse(TEXT("there is no +0 stone"), UCataclysmForge::CombineOf(0, 9, 1, 1000000).bPossible);
	TestTrue(TEXT("+9 makes the +10"), UCataclysmForge::CombineOf(9, 2, 1, 1000000).bPossible);
	AddInfo(FString::Printf(TEXT("COMBINE RULES: at +10: %s"), *UCataclysmForge::CombineOf(10, 9, 1, 1000000).Refusal));
	return true;
}

// ---------------------------------------------------------------------------
// The crafts, done to the character playing
// ---------------------------------------------------------------------------

/**
 * A carried piece is raised in its own cell: two stones of the next level leave the bag, it carries 25 more residue,
 * and the run's day moves on by the craft's days -- one below 100 residue, two from 100. A piece with no stones of the
 * next level is refused and nothing changes. Prints "CARRIED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeCarriedTest,
	"Cataclysm.Forge.UpgradingACarriedPieceRaisesItInItsCellTakesTheStonesAndPassesTheDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeCarriedTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	FForgeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(PlainOf(GlovesBase, 0, 50.0f));
	Scene.Bag()->AddMaterial(Stone(1), 5);
	const int32 DayBefore = Scene.Run->Day();

	FCataclysmForgeUpgrade Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	const FCataclysmItem* Raised = Scene.Bag()->ItemAt(Cell);

	AddInfo(FString::Printf(TEXT("CARRIED: result %d '%s'; cell %d holds +%d with residue %.0f; %d +1 stones left; day "
								 "%d to %d"),
							static_cast<int32>(Result), *Message, Cell, Raised ? Raised->GearLevel : -1,
							Raised ? Raised->Residue : -1.0f, Scene.Bag()->CountOfMaterial(Stone(1)), DayBefore,
							Scene.Run->Day()));
	TestEqual(TEXT("the upgrade is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the piece is still in its cell"), Raised))
	{
		return false;
	}
	TestEqual(TEXT("raised to +1"), Raised->GearLevel, 1);
	TestEqual(TEXT("still the same base"), Raised->Base, FName(GlovesBase));
	TestEqual(TEXT("carrying 25 more residue"), Raised->Residue, 75.0f, 0.001f);
	TestEqual(TEXT("two +1 stones gone, three left"), Scene.Bag()->CountOfMaterial(Stone(1)), 3);
	TestEqual(TEXT("one day passed"), Scene.Run->Day(), DayBefore + 1);

	// NO +2 STONE: refused, and nothing moves.
	const int32 DayRefused = Scene.Run->Day();
	const ECataclysmForgeResult Refused =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	TestEqual(TEXT("with no +2 stone the next upgrade is refused"), static_cast<int32>(Refused),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying what it takes"), Message,
			  FString(TEXT("Raising it to +2 takes two +2 upgrade stones; 0 held.")));
	TestEqual(TEXT("the piece stays +1"), Scene.Bag()->ItemAt(Cell)->GearLevel, 1);
	TestEqual(TEXT("and no day passes"), Scene.Run->Day(), DayRefused);

	// 100 RESIDUE OR MORE: the time penalty's day.
	const int32 Heavy = Scene.Bag()->AddItem(PlainOf(BootsBase, 0, 150.0f));
	const int32 DayHeavy = Scene.Run->Day();
	UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Heavy), false, Done, Message);
	TestEqual(TEXT("a piece carrying 150 residue costs two days"), Scene.Run->Day(), DayHeavy + 2);
	TestEqual(TEXT("and says so"), Done.Days, 2);
	return true;
}

/**
 * A worn weapon is raised in its slot and the wearer's attack damage rises with it: a Greataxe's 72 is stated at +10,
 * which is 29 at +0 since issue #2883, and a two-handed weapon doubles it inside the rounding, so +0 is 58 and +1 is
 * 29 x 1.15 x 2 = 66.7, 67 with a half going up: a rise of 9 (57.6 to 66.24, 8.64, before). Prints "WORN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeWornTest,
	"Cataclysm.Forge.UpgradingAWornWeaponRaisesItInItsSlotAndTheAttackDamageWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeWornTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	FForgeScene Scene = MakeScene();
	UAbilitySystemComponent* Abilities = Scene.Character ? Scene.Character->GetAbilitySystemComponent() : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: with an ability system"), Abilities))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const ECataclysmGearSlot Slot = Wear(Scene, PlainOf(GreataxeBase, 0, 40.0f));
	Scene.Bag()->AddMaterial(Stone(1), 2);
	const FGameplayAttribute Damage = UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	const float Before = Abilities->GetNumericAttribute(Damage);

	FCataclysmForgeUpgrade Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Worn(Slot), false, Done, Message);
	const float After = Abilities->GetNumericAttribute(Damage);
	const FCataclysmItem* Raised = Scene.Worn()->EquippedAt(Slot);

	AddInfo(FString::Printf(TEXT("WORN: result %d '%s'; slot %d holds +%d; attack damage %.2f to %.2f"),
							static_cast<int32>(Result), *Message, static_cast<int32>(Slot),
							Raised ? Raised->GearLevel : -1, Before, After));
	TestTrue(TEXT("set-up: the Greataxe went to the first weapon slot"), Slot == ECataclysmGearSlot::Weapon1);
	TestEqual(TEXT("the upgrade is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the weapon is still worn"), Raised))
	{
		return false;
	}
	TestEqual(TEXT("raised to +1 in its slot"), Raised->GearLevel, 1);
	TestEqual(TEXT("the attack damage rises from 58 to 67, by 9"), After - Before, 9.0f, 0.05f);
	return true;
}

/**
 * Raising a worn piece that would take Worn Residue from below the tier's Consumption Threshold to at or past it
 * asks first and changes nothing; confirmed, it raises the piece, marks the character and captures the build its
 * double wears. A carried piece's upgrade never asks: its residue is not worn. Prints "THRESHOLD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeThresholdTest,
	"Cataclysm.Forge.AWornUpgradeThatWouldCrossTheThresholdAsksAndMarksOnlyWhenConfirmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	FForgeScene Scene = MakeScene();
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
	Scene.Bag()->AddMaterial(Stone(1), 6);

	// CONTROL: A CARRIED PIECE NEVER ASKS, whatever it carries.
	const int32 Cell = Scene.Bag()->AddItem(PlainOf(GlovesBase, 0, Threshold - 10.0f));
	FCataclysmForgeUpgrade Done;
	FString Message;
	const ECataclysmForgeResult Carried =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	TestEqual(TEXT("CONTROL: a carried piece is raised without a question"), static_cast<int32>(Carried),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestFalse(TEXT("CONTROL: and the character is not marked"), Scene.State->IsMarkedByConsumption());

	// A HELM WORN 10 BELOW THE THRESHOLD, so its 25 takes the Worn Residue past it.
	const ECataclysmGearSlot Head = Wear(Scene, PlainOf(HelmBase, 0, Threshold - 10.0f - Scene.Worn()->WornResidue()));
	const float WornBefore = Scene.Worn()->WornResidue();
	const int32 StonesBefore = Scene.Bag()->CountOfMaterial(Stone(1));
	const int32 DayBefore = Scene.Run->Day();

	const ECataclysmForgeResult Asked =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Worn(Head), false, Done, Message);
	TestEqual(TEXT("the worn helm's upgrade asks first"), static_cast<int32>(Asked),
			  static_cast<int32>(ECataclysmForgeResult::AsksAboutTheThreshold));
	TestTrue(TEXT("naming the threshold"), Message.Contains(TEXT("Consumption Threshold")));
	TestTrue(TEXT("and saying it is an upgrade"), Message.StartsWith(TEXT("Upgrading ")));
	TestEqual(TEXT("asking changes nothing: the helm is +0"), Scene.Worn()->EquippedAt(Head)->GearLevel, 0);
	TestEqual(TEXT("nor the stones"), Scene.Bag()->CountOfMaterial(Stone(1)), StonesBefore);
	TestEqual(TEXT("nor the day"), Scene.Run->Day(), DayBefore);
	TestFalse(TEXT("nor marks the character"), Scene.State->IsMarkedByConsumption());

	const ECataclysmForgeResult Confirmed =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Worn(Head), true, Done, Message);
	AddInfo(FString::Printf(TEXT("THRESHOLD: %.0f; worn %.0f before, %.0f after; asked '%s'; confirmed %d, marked %s, "
								 "build held %s"),
							Threshold, WornBefore, Scene.Worn()->WornResidue(), *Message, static_cast<int32>(Confirmed),
							Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no"),
							Scene.State->HoldsConsumptionBuild() ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("confirmed, it is done"), static_cast<int32>(Confirmed),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestEqual(TEXT("the helm is +1"), Scene.Worn()->EquippedAt(Head)->GearLevel, 1);
	TestTrue(TEXT("the Worn Residue is past the threshold"), Scene.Worn()->WornResidue() >= Threshold);
	TestTrue(TEXT("the character is marked"), Scene.State->IsMarkedByConsumption());
	TestTrue(TEXT("and the build its double wears is held"), Scene.State->HoldsConsumptionBuild());
	return true;
}

/**
 * During a walk both crafts are refused with the forge's walk sentence and nothing changes; with no run an upgrade is
 * refused, having no run to take its days from, while a combine, which passes no day, is done. Prints "REFUSED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeRefusalsTest,
	"Cataclysm.Forge.TheForgeRefusesDuringAWalkAndAnUpgradeWithNoRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeRefusalsTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	FForgeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(PlainOf(GlovesBase, 0, 50.0f));
	Scene.Bag()->AddMaterial(Stone(1), 4);

	// IN A WALK: the clock says a dungeon is being stood in, as entering one does.
	Scene.Run->Clock->EnterDungeon(/*DungeonId*/ 7);
	TestTrue(TEXT("CONTROL: the run says a walk is under way"), Scene.Run->IsAWalkUnderWay());
	const int32 DayInTheWalk = Scene.Run->Day();

	FCataclysmForgeUpgrade Done;
	FString Message;
	const ECataclysmForgeResult InTheWalk =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	TestEqual(TEXT("an upgrade during a walk is refused"), static_cast<int32>(InTheWalk),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("with the forge's walk sentence"), Message, FString(UCataclysmForgeCraft::WalkRefusal));
	FCataclysmForgeCombine Combined;
	FString Reason;
	TestFalse(TEXT("a combine during a walk is refused"),
			  UCataclysmForgeCraft::Combine(Scene.World, 1, Combined, Reason));
	TestEqual(TEXT("with the same sentence"), Reason, FString(UCataclysmForgeCraft::WalkRefusal));
	TestEqual(TEXT("the piece is unchanged"), Scene.Bag()->ItemAt(Cell)->GearLevel, 0);
	TestEqual(TEXT("so are the stones"), Scene.Bag()->CountOfMaterial(Stone(1)), 4);
	TestEqual(TEXT("and the day"), Scene.Run->Day(), DayInTheWalk);

	// NO RUN: an upgrade has no days to take; a combine passes none.
	Scene.Instance->EmpireRun = nullptr;
	const ECataclysmForgeResult NoRun =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	TestEqual(TEXT("with no run an upgrade is refused"), static_cast<int32>(NoRun),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying why"), Message, FString(TEXT("There is no run to take the upgrade's days from.")));
	TestTrue(TEXT("while a combine is done"), UCataclysmForgeCraft::Combine(Scene.World, 1, Combined, Reason));
	AddInfo(FString::Printf(TEXT("REFUSED: in the walk '%s'; with no run '%s'; a combine with no run left %d +1 and %d "
								 "+2"),
							*FString(UCataclysmForgeCraft::WalkRefusal), *Message,
							Scene.Bag()->CountOfMaterial(Stone(1)), Scene.Bag()->CountOfMaterial(Stone(2))));
	TestEqual(TEXT("two +1 stones made one +2"), Scene.Bag()->CountOfMaterial(Stone(2)), 1);
	return true;
}

/** Combining three +2 stones leaves one +2 and one +3, and passes no day; a second combine is refused. Prints
 *  "COMBINE". */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeCombineTest,
	"Cataclysm.Forge.CombiningInTheBagMakesOneStoneOfTheNextLevelAndPassesNoDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeCombineTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	FForgeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	Scene.Bag()->AddMaterial(Stone(2), 3);
	const int32 DayBefore = Scene.Run->Day();

	FCataclysmForgeCombine Done;
	FString Reason;
	TestTrue(TEXT("three +2 stones combine"), UCataclysmForgeCraft::Combine(Scene.World, 2, Done, Reason));
	TestEqual(TEXT("one +2 is left"), Scene.Bag()->CountOfMaterial(Stone(2)), 1);
	TestEqual(TEXT("one +3 is made"), Scene.Bag()->CountOfMaterial(Stone(3)), 1);
	TestEqual(TEXT("no day passes"), Scene.Run->Day(), DayBefore);

	TestFalse(TEXT("one +2 cannot combine"), UCataclysmForgeCraft::Combine(Scene.World, 2, Done, Reason));
	TestEqual(TEXT("and says so"), Reason, FString(TEXT("Combining takes two +2 upgrade stones; 1 held.")));
	AddInfo(FString::Printf(TEXT("COMBINE: %d +2 and %d +3 held; then '%s'"), Scene.Bag()->CountOfMaterial(Stone(2)),
							Scene.Bag()->CountOfMaterial(Stone(3)), *Reason));
	return true;
}

/**
 * An upgrade in a frame whose run record was already written still writes it, so the record read back holds the day
 * the upgrade's days reached: `ForgeCrafted` is in `WritesEvenIfTheRunWasWrittenThisFrame`, for the respec's reason.
 * Prints "SAME FRAME".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeSameFrameTest,
	"Cataclysm.Forge.AnUpgradeInAFrameAlreadyWrittenStillWritesItsDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeSameFrameTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	FForgeScene Scene = MakeScene(/*bWriting=*/true);
	UCataclysmSaveWriter* Writer = Scene.World ? UCataclysmSaveWriter::In(Scene.World) : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestTrue(TEXT("set-up: and its save writer is writing"), Writer != nullptr && Writer->IsWriting()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Writer->RunSlotName();
	const FString CharacterSlot = Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };

	const int32 Cell = Scene.Bag()->AddItem(PlainOf(GlovesBase, 0, 50.0f));
	Scene.Bag()->AddMaterial(Stone(1), 2);

	// THE RUN RECORD WRITTEN FIRST THIS FRAME, by an item's trigger. A test's frame does not end.
	const int32 Start = Writer->WritesStarted();
	Writer->NoteTrigger(ECataclysmSaveTrigger::InventoryChanged);
	const int32 AfterItem = Writer->WritesStarted();
	// CONTROL: THE ONCE-A-FRAME RULE IS IN FORCE. A clock write in the same frame is skipped.
	Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	const int32 AfterClock = Writer->WritesStarted();

	FCataclysmForgeUpgrade Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	const int32 AfterUpgrade = Writer->WritesStarted();
	const ECataclysmSaveTrigger Last = Writer->LastTrigger();
	WaitForTheSlot(RunSlot);

	ECataclysmSaveLoadResult ReadResult = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString ReadMessage;
	const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::ReadFromSlot(
		RunSlot, UCataclysmSaveWriter::UserIndex, UCataclysmRunSave::StaticClass(), GetTransientPackage(), ReadResult,
		ReadMessage));

	AddInfo(FString::Printf(TEXT("SAME FRAME: the item's trigger started %d write(s), the clock after it %d, the upgrade "
								 "%d; last trigger '%s'; the run is on day %d and '%s' read back day %d"),
							AfterItem - Start, AfterClock - AfterItem, AfterUpgrade - AfterClock,
							UCataclysmSaveTriggers::NameOf(Last), Scene.Run->Day(), *RunSlot,
							Read != nullptr ? Read->Day : -1));
	TestTrue(TEXT("set-up: the item's trigger wrote the run record this frame"), AfterItem > Start);
	TestEqual(TEXT("CONTROL: a clock write in the same frame is skipped"), AfterClock, AfterItem);
	TestEqual(TEXT("set-up: the upgrade is done"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestTrue(TEXT("the upgrade starts writes in the same frame"), AfterUpgrade > AfterClock);
	TestEqual(TEXT("the writer was told of a craft at the forge, last"), static_cast<int32>(Last),
			  static_cast<int32>(ECataclysmSaveTrigger::ForgeCrafted));
	if (!TestNotNull(TEXT("the run record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("and holds the day the upgrade's day reached"), Read->Day, Scene.Run->Day());
	return true;
}

namespace CataclysmForgeTest
{
	/** The PLAY LOOP lines said while it lives, in order, without their prefix. Issue #2857. */
	struct FForgeDaysLines : public FOutputDevice
	{
		TArray<FString> Lines;

		FForgeDaysLines()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
		}

		virtual ~FForgeDaysLines() override
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(this);
			}
		}

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const FString Line(Text);
			if (Category == FName(TEXT("LogCataclysm")) && Line.StartsWith(FCataclysmPlayLoopLog::Prefix))
			{
				Lines.Add(Line.RightChop(FCString::Strlen(FCataclysmPlayLoopLog::Prefix)));
			}
		}

		/** What was said, after the log is flushed: a line written off the game thread is buffered until then. */
		const TArray<FString>& Said()
		{
			if (GLog)
			{
				GLog->Flush();
			}
			return Lines;
		}
	};

	/**
	 * The run's opening wave fired, ONE ORDINARY DUNGEON on a standing city -- not a bonus dungeon, and not a Void
	 * dungeon, whose hit takes no defence -- set to run out after `Days` days, and every other timer pushed to 60, so
	 * the test knows which dungeon its days run out. The dungeon, or INDEX_NONE when the wave stood none.
	 */
	int32 ForgeDungeonDueIn(UCataclysmEmpireRun& Run, float Days)
	{
		Run.FireOpeningSurge();
		int32 Due = INDEX_NONE;
		for (const FCataclysmDungeon& Dungeon : Run.Dungeons)
		{
			const FCataclysmCity* City = Run.Map != nullptr ? Run.Map->Find(Dungeon.CityId) : nullptr;
			if (Dungeon.Type == ECataclysmDungeonType::Basic && !Dungeon.bBonus && !Dungeon.AddsVoidStacks()
				&& City != nullptr && !City->bFallen)
			{
				Due = Dungeon.DungeonId;
				break;
			}
		}
		if (Due != INDEX_NONE && Run.Clock != nullptr)
		{
			for (FCataclysmDungeonTimer& Timer : Run.Clock->Timers)
			{
				Timer.DaysUntilResolve = Timer.DungeonId == Due ? Days : 60.0f;
			}
		}
		return Due;
	}
}

/**
 * Issue #2857: the owner saw dungeons gone after days spent at the forge, with nothing saying why, because the forge ran
 * its days and threw their reports away. An upgrade whose three days run an ordinary dungeon's timer out now says so:
 * a PLAY LOOP line after the upgrade's own, "at the forge, days 0 to 3: dungeons resolved: 7 (the dungeon on ...);
 * cities hit: ... lost ...", and the same account, after "Meanwhile", kept for the forge's screen. The dungeon, having
 * hit its city, is gone from the run. Prints "FORGE DAYS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeDaysReportedTest,
	"Cataclysm.Forge.AnUpgradeWhoseDaysRunOutATimerSaysWhatResolvedAndTheCityItHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeDaysReportedTest::RunTest(const FString&)
{
	using namespace CataclysmForgeTest;

	FForgeScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Due = ForgeDungeonDueIn(*Scene.Run, 3.0f);
	const FCataclysmDungeon* Dungeon = Due != INDEX_NONE ? Scene.Run->FindDungeon(Due) : nullptr;
	const FCataclysmCity* City = Dungeon != nullptr ? Scene.Run->Map->Find(Dungeon->CityId) : nullptr;
	if (!TestNotNull(TEXT("set-up: the opening wave stood an ordinary dungeon on a standing city"), City))
	{
		return false;
	}
	const FString DueName = Scene.Run->NameOfDungeon(Due);
	const FString CityName = City->Name;
	const int32 DayBefore = Scene.Run->Day();

	// A +0 PIECE CARRYING 250 RESIDUE: a day for the level it reaches and two for the residue, the dungeon's three.
	const int32 Cell = Scene.Bag()->AddItem(PlainOf(GlovesBase, 0, 250.0f));
	Scene.Bag()->AddMaterial(Stone(1), 2);

	FForgeDaysLines Capture;
	FCataclysmForgeUpgrade Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	const TArray<FString> Said = Capture.Said();
	const FString Kept = UCataclysmGameInstance::CapitalDaysAccountIn(Scene.World);

	const FString Lead = FString::Printf(TEXT("at the forge, days %d to %d: "), DayBefore, DayBefore + 3);
	// THE DUNGEON BY ITS NUMBER AND NAME, which only the resolved part says; ITS CITY AMONG THOSE HIT, not necessarily
	// first: a Siege's daily share is a hit too.
	const FString Resolved = FString::Printf(TEXT("%d (%s)"), Due, *DueName);
	const FString Hit = FString::Printf(TEXT("%s lost "), *CityName);
	const int32 UpgradeAt = Said.IndexOfByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("upgraded ")); });
	const int32 AccountAt = Said.IndexOfByPredicate([&Lead](const FString& Line) { return Line.StartsWith(Lead); });
	AddInfo(FString::Printf(TEXT("FORGE DAYS: dungeon %d (%s) due in 3 on %s; result %d '%s', %d days, day %d to %d; said: "
								 "%s; kept: \"%s\""),
							Due, *DueName, *CityName, static_cast<int32>(Result), *Message, Done.Days, DayBefore,
							Scene.Run->Day(), *FString::Join(Said, TEXT(" | ")), *Kept));

	TestEqual(TEXT("the upgrade is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	TestEqual(TEXT("set-up: in three days"), Done.Days, 3);
	TestEqual(TEXT("the run is three days on"), Scene.Run->Day(), DayBefore + 3);
	TestNull(TEXT("CONTROL: the dungeon is gone, as an ordinary dungeon that hit its city leaves"),
			 Scene.Run->FindDungeon(Due));
	if (TestTrue(TEXT("a PLAY LOOP line says what the forge's days did"), Said.IsValidIndex(AccountAt)))
	{
		TestTrue(TEXT("naming the dungeon that resolved, by the name it had before the days"),
				 Said[AccountAt].Contains(Resolved));
		TestTrue(TEXT("and the city it hit, with what that city lost"),
				 Said[AccountAt].Contains(Hit) && !Said[AccountAt].Contains(TEXT("cities hit: none")));
	}
	TestTrue(TEXT("said after the upgrade's own line"), UpgradeAt != INDEX_NONE && AccountAt > UpgradeAt);
	TestTrue(TEXT("the same account is kept for the screen, after \"Meanwhile\""),
			 Kept.StartsWith(FString::Printf(TEXT("Meanwhile, days %d to %d: "), DayBefore, DayBefore + 3))
				 && Kept.Contains(Resolved) && Kept.Contains(Hit) && Kept.EndsWith(TEXT(".")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
