// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "DayClock/CataclysmDayClock.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE FORGE'S ADD SOCKET AND CHANGE SOCKET TYPE. Issue #46, slice G9, ruled by the coordinating session on 2026-11-22.
 *
 * The Crafting sheet's rows: Add Socket, a Shattered Core, 15 residue, 3 base days, Base Gold 750; Change Socket Type,
 * a Focusing Lens, 10 residue, 2 base days, Base Gold 500; the residue's time penalty and the gold formula on both. An
 * added socket is empty and its type rolled evenly, from the item's saved contents, the run's mask and the day; a
 * changed socket takes the type chosen. The scene is the one `CataclysmForgeGemsTests.cpp` builds, copied into this
 * file's namespace because the module is built in unity files.
 */
namespace CataclysmForgeSocketsTest
{
	const FName Core(UCataclysmForge::ShatteredCoreRow);
	const FName Lens(UCataclysmForge::FocusingLensRow);
	const FName Viper(TEXT("Gem_Of_The_Viper"));

	FCataclysmSocket MakeSocket(ECataclysmSocketType Type, FName Gem = NAME_None)
	{
		FCataclysmSocket Out;
		Out.Type = Type;
		Out.Gem = Gem;
		return Out;
	}

	/** A helm carrying `Residue` and these sockets. A Head takes 2 (`game/Data/ItemSockets.csv`). */
	FCataclysmItem Helm(float Residue, const TArray<FCataclysmSocket>& Sockets)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.Residue = Residue;
		Item.Sockets = Sockets;
		return Item;
	}

	/**
	 * FS2'S FIXED ITEM: a gear level 3 helm with one affix and one empty Defense socket, residue 40. Its seed text and
	 * the types below were computed with the Python copy of `UCataclysmForge::AddedSocketType` in
	 * `tools/tests/test_the_forge_adds_and_changes_sockets.py`.
	 */
	FCataclysmItem SeedHelm(float Roll)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = 3;
		FCataclysmRolledAffix& Affix = Item.Affixes.AddDefaulted_GetRef();
		Affix.Affix = FName(TEXT("Stat_Flat_evasion"));
		Affix.Tier = 2;
		Affix.Roll = Roll;
		Item.Sockets = {MakeSocket(ECataclysmSocketType::Defense)};
		Item.Residue = 40.0f;
		return Item;
	}

	struct FForgeSocketsScene
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
	FForgeSocketsScene MakeScene()
	{
		FForgeSocketsScene Out;
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
			Out.State->SetIdentity(TEXT("Socketer"), Partition);
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
}

// --------------------------------------------------------------------------------------------------------------------
// FS1. Adding a socket: the rule's figures, its question and refusals, and the craft on a carried helm.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeSocketsAddTest,
	"Cataclysm.ForgeSockets.AddingASocketTakesAShatteredCore15ResidueThreeDaysAndItsGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeSocketsAddTest::RunTest(const FString&)
{
	using namespace CataclysmForgeSocketsTest;
	const FCataclysmItem OneSocket = Helm(87.0f, {MakeSocket(ECataclysmSocketType::Defense)});

	const FCataclysmForgeSocketWork Plan = UCataclysmForge::AddSocketOf(OneSocket, 2, 1, 1, 1000000);
	TestTrue(TEXT("a helm with 1 of its 2 sockets can take another"), Plan.bPossible);
	TestEqual(TEXT("with a Shattered Core"), Plan.Material, Core);
	TestEqual(TEXT("it becomes socket 2"), Plan.Socket, 1);
	TestEqual(TEXT("residue 87 becomes 102"), Plan.ResidueAfter, 102.0f, 0.001f);
	TestEqual(TEXT("three days below 100 residue"), Plan.Days, 3);
	TestEqual(TEXT("750 gold a time, at 87 residue 2,055"), Plan.Gold, static_cast<int64>(2055));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::SocketWorkQuestion(Plan),
			  FString(TEXT("Press again to add a socket: one Shattered Core, 2,055 gold, residue 87 to 102, 3 days of "
						   "the run; its type is rolled.")));
	TestEqual(TEXT("no Shattered Core"), UCataclysmForge::AddSocketOf(OneSocket, 2, 0, 1, 1000000).Refusal,
			  FString(TEXT("Adding a socket takes a Shattered Core; 0 held.")));
	TestEqual(TEXT("too little gold"), UCataclysmForge::AddSocketOf(OneSocket, 2, 1, 1, 100).Refusal,
			  FString(TEXT("Adding a socket takes 2,055 gold; 100 gold held.")));
	TestEqual(TEXT("no piece"), UCataclysmForge::AddSocketOf(FCataclysmItem(), 2, 1, 1, 1000000).Refusal,
			  FString(TEXT("There is nothing there to add a socket to.")));

	FForgeSocketsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(OneSocket);
	Scene.Bag()->AddMaterial(Core, 1);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();

	FCataclysmForgeSocketWork Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::AddSocket(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the socket is added"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After) || !TestEqual(TEXT("it has 2 sockets"), After->Sockets.Num(), 2))
	{
		return false;
	}
	const FCataclysmSocket& Added = After->Sockets[1];
	TestTrue(TEXT("the new socket is empty"), Added.Gem.IsNone());
	TestTrue(TEXT("of Attack, Defense or Utility, never Any"), Added.Type != ECataclysmSocketType::Any);
	TestTrue(TEXT("the type the craft reports"), Added.Type == Done.ToType);
	TestTrue(TEXT("socket 1 is as it was"), After->Sockets[0].Type == ECataclysmSocketType::Defense);
	TestEqual(TEXT("residue 87 becomes 102"), After->Residue, 102.0f, 0.001f);
	TestEqual(TEXT("the Shattered Core is used"), Scene.Bag()->CountOfMaterial(Core), 0);
	TestEqual(TEXT("three days passed"), Scene.Run->Day(), DayBefore + 3);
	TestEqual(TEXT("2,055 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(2055));

	// A SECOND ADDITION: the helm holds its 2 now, so it is refused and nothing moves.
	const ECataclysmForgeResult Refused =
		UCataclysmForgeCraft::AddSocket(Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message);
	TestEqual(TEXT("a third socket on a helm is refused"), static_cast<int32>(Refused),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying so"), Message, FString(TEXT("That piece holds its most sockets, 2.")));
	TestEqual(TEXT("and no day passed"), Scene.Run->Day(), DayBefore + 3);
	AddInfo(FString::Printf(TEXT("SOCKET ADDED: %s socket, residue %.0f, day %d to %d, gold %lld; then '%s'"),
							*UCataclysmGems::SocketTypeName(Added.Type), After->Residue, DayBefore, Scene.Run->Day(),
							GoldBefore - Scene.State->GetOwnGold(), *Message));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FS2. An added socket's type: the item's seed text, the same type for the same item, run and day, a different type
// for an item differing in one affix roll, and an even spread over the days. Every figure computed in Python.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeSocketsRollTest,
	"Cataclysm.ForgeSockets.AnAddedSocketsTypeIsRolledEvenlyAndTheSameForTheSameRunDayAndItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeSocketsRollTest::RunTest(const FString&)
{
	using namespace CataclysmForgeSocketsTest;
	const FCataclysmItem A = SeedHelm(0.5f);
	const FCataclysmItem B = SeedHelm(0.625f);
	constexpr int32 Mask = 12345;
	constexpr int32 Day = 7;

	TestEqual(TEXT("the item's seed text, as the Python copy writes it"), UCataclysmForge::ItemSeedText(A),
			  FString(TEXT("Base=Head_Helm;Level=3;Types=;Affixes=Stat_Flat_evasion,2,0.500000,;Enchantments=0:;"
						   "Sockets=Defense,None,0;Residue=40.000000;Kills=0;Made=0")));
	TestEqual(TEXT("its seed on day 7 of mask 12345"), UCataclysmForge::AddedSocketSeed(A, Mask, Day), 1581167466);
	const ECataclysmSocketType First = UCataclysmForge::AddedSocketType(A, Mask, Day);
	TestTrue(TEXT("rolls Attack"), First == ECataclysmSocketType::Attack);
	TestTrue(TEXT("and again Attack: a reload on the same day rolls the same"),
			 UCataclysmForge::AddedSocketType(A, Mask, Day) == First);
	TestTrue(TEXT("the same helm with its affix rolled 0.625 instead of 0.5 rolls Utility"),
			 UCataclysmForge::AddedSocketType(B, Mask, Day) == ECataclysmSocketType::Utility);

	int32 Attack = 0;
	int32 Defense = 0;
	int32 Utility = 0;
	for (int32 Each = 1; Each <= 3000; ++Each)
	{
		switch (UCataclysmForge::AddedSocketType(A, Mask, Each))
		{
		case ECataclysmSocketType::Attack:	++Attack; break;
		case ECataclysmSocketType::Defense:	++Defense; break;
		case ECataclysmSocketType::Utility:	++Utility; break;
		default: break;
		}
	}
	AddInfo(FString::Printf(TEXT("ADDED SOCKET ROLLS: seed %d, %s; over days 1 to 3000 Attack %d, Defense %d, Utility %d"),
							UCataclysmForge::AddedSocketSeed(A, Mask, Day), *UCataclysmGems::SocketTypeName(First),
							Attack, Defense, Utility));
	TestEqual(TEXT("over days 1 to 3000: Attack 1075, as computed"), Attack, 1075);
	TestEqual(TEXT("Defense 966"), Defense, 966);
	TestEqual(TEXT("Utility 959"), Utility, 959);
	TestTrue(TEXT("each within a tenth of a third"), FMath::Abs(Attack - 1000) <= 100 && FMath::Abs(Defense - 1000) <= 100
														 && FMath::Abs(Utility - 1000) <= 100);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FS3. The cap is the slot's maximum: a helm 2, a ring 1, a two-handed weapon 6.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeSocketsCapTest,
	"Cataclysm.ForgeSockets.AnItemAtItsSlotsMaximumIsRefusedAndATwoHanderReachesSix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeSocketsCapTest::RunTest(const FString&)
{
	using namespace CataclysmForgeSocketsTest;
	FCataclysmItem Greatsword;
	Greatsword.Base = FName(TEXT("Weapon_Greatsword"));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Greatsword.Sockets.Add(MakeSocket(ECataclysmSocketType::Attack));
	}
	FCataclysmItem Ring;
	Ring.Base = FName(TEXT("Ring_Band"));
	Ring.Sockets.Add(MakeSocket(ECataclysmSocketType::Utility));
	FCataclysmItem Unknown;
	Unknown.Base = FName(TEXT("No_Such_Base"));

	const int32 GreatswordMax = UCataclysmForgeCraft::MaxSocketsOf(Greatsword);
	const int32 RingMax = UCataclysmForgeCraft::MaxSocketsOf(Ring);
	AddInfo(FString::Printf(TEXT("SOCKET CAPS: helm %d, ring %d, greatsword %d, unknown %d"),
							UCataclysmForgeCraft::MaxSocketsOf(Helm(0.0f, {})), RingMax, GreatswordMax,
							UCataclysmForgeCraft::MaxSocketsOf(Unknown)));
	TestEqual(TEXT("a helm takes 2"), UCataclysmForgeCraft::MaxSocketsOf(Helm(0.0f, {})), 2);
	TestEqual(TEXT("a ring 1"), RingMax, 1);
	TestEqual(TEXT("a two-handed greatsword 6"), GreatswordMax, 6);
	TestEqual(TEXT("an unknown base is not known"), UCataclysmForgeCraft::MaxSocketsOf(Unknown), -1);

	TestTrue(TEXT("the greatsword with 5 can take a sixth"),
			 UCataclysmForge::AddSocketOf(Greatsword, GreatswordMax, 1, 1, 1000000).bPossible);
	Greatsword.Sockets.Add(MakeSocket(ECataclysmSocketType::Defense));
	TestEqual(TEXT("with 6 it is refused, naming the maximum"),
			  UCataclysmForge::AddSocketOf(Greatsword, GreatswordMax, 1, 1, 1000000).Refusal,
			  FString(TEXT("That piece holds its most sockets, 6.")));
	TestEqual(TEXT("the ring with 1 is refused"), UCataclysmForge::AddSocketOf(Ring, RingMax, 1, 1, 1000000).Refusal,
			  FString(TEXT("That piece holds its most sockets, 1.")));
	TestEqual(TEXT("an unknown maximum is refused"), UCataclysmForge::AddSocketOf(Unknown, -1, 1, 1, 1000000).Refusal,
			  FString(TEXT("How many sockets that piece can take is not known.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FS4. Changing a socket's type: the rule's figures and question, and the craft on a carried helm.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeSocketsChangeTest,
	"Cataclysm.ForgeSockets.ChangingASocketsTypeTakesAFocusingLens10ResidueTwoDaysAndItsGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeSocketsChangeTest::RunTest(const FString&)
{
	using namespace CataclysmForgeSocketsTest;
	const FCataclysmItem Item = Helm(87.0f, {MakeSocket(ECataclysmSocketType::Defense)});

	const FCataclysmForgeSocketWork Plan =
		UCataclysmForge::ChangeSocketTypeOf(Item, 0, ECataclysmSocketType::Utility, 1, 1, 1000000);
	TestTrue(TEXT("the empty Defense socket can be made Utility"), Plan.bPossible);
	TestEqual(TEXT("with a Focusing Lens"), Plan.Material, Lens);
	TestEqual(TEXT("residue 87 becomes 97"), Plan.ResidueAfter, 97.0f, 0.001f);
	TestEqual(TEXT("two days below 100 residue"), Plan.Days, 2);
	TestEqual(TEXT("500 gold a time, at 87 residue 1,370"), Plan.Gold, static_cast<int64>(1370));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::SocketWorkQuestion(Plan),
			  FString(TEXT("Press again to make socket 1 a Utility socket: one Focusing Lens, 1,370 gold, residue 87 to "
						   "97, 2 days of the run.")));

	FForgeSocketsScene Scene = MakeScene();
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
	Scene.Bag()->AddMaterial(Lens, 1);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	FCataclysmForgeSocketWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::ChangeSocketType(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, ECataclysmSocketType::Utility, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the change is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	TestTrue(TEXT("socket 1 is a Utility socket"), After->Sockets[0].Type == ECataclysmSocketType::Utility);
	TestEqual(TEXT("and still the only one"), After->Sockets.Num(), 1);
	TestEqual(TEXT("residue 87 becomes 97"), After->Residue, 97.0f, 0.001f);
	TestEqual(TEXT("the Focusing Lens is used"), Scene.Bag()->CountOfMaterial(Lens), 0);
	TestEqual(TEXT("two days passed"), Scene.Run->Day(), DayBefore + 2);
	TestEqual(TEXT("1,370 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(1370));
	AddInfo(FString::Printf(TEXT("SOCKET CHANGED: %s, residue %.0f, day %d to %d"),
							*UCataclysmGems::SocketTypeName(After->Sockets[0].Type), After->Residue, DayBefore,
							Scene.Run->Day()));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FS5. A change is refused for a socket holding a gem, its own type, a socket for any gem, a new type of Any and no
// Focusing Lens; refused on the character, nothing moves.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeSocketsRefusedTest,
	"Cataclysm.ForgeSockets.AChangeIsRefusedForAFullSocketItsOwnTypeAndAnAnySocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeSocketsRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmForgeSocketsTest;
	const FCataclysmItem Item = Helm(87.0f, {MakeSocket(ECataclysmSocketType::Defense),
											 MakeSocket(ECataclysmSocketType::Attack, Viper)});
	const FCataclysmItem Old = Helm(87.0f, {MakeSocket(ECataclysmSocketType::Any)});

	TestEqual(TEXT("a socket holding a gem"),
			  UCataclysmForge::ChangeSocketTypeOf(Item, 1, ECataclysmSocketType::Defense, 1, 1, 1000000).Refusal,
			  FString(TEXT("Unsocket the gem first.")));
	TestEqual(TEXT("its own type"),
			  UCataclysmForge::ChangeSocketTypeOf(Item, 0, ECataclysmSocketType::Defense, 1, 1, 1000000).Refusal,
			  FString(TEXT("That socket is a Defense socket already.")));
	TestEqual(TEXT("a socket for any gem"),
			  UCataclysmForge::ChangeSocketTypeOf(Old, 0, ECataclysmSocketType::Attack, 1, 1, 1000000).Refusal,
			  FString(TEXT("That socket fits any gem already.")));
	TestEqual(TEXT("a new type of Any"),
			  UCataclysmForge::ChangeSocketTypeOf(Item, 0, ECataclysmSocketType::Any, 1, 1, 1000000).Refusal,
			  FString(TEXT("A socket can be made Attack, Defense or Utility, not one for any gem.")));
	TestEqual(TEXT("no Focusing Lens"),
			  UCataclysmForge::ChangeSocketTypeOf(Item, 0, ECataclysmSocketType::Attack, 0, 1, 1000000).Refusal,
			  FString(TEXT("Changing a socket's type takes a Focusing Lens; 0 held.")));
	TestEqual(TEXT("no such socket"),
			  UCataclysmForge::ChangeSocketTypeOf(Item, 4, ECataclysmSocketType::Attack, 1, 1, 1000000).Refusal,
			  FString(TEXT("That piece has no socket 5.")));

	FForgeSocketsScene Scene = MakeScene();
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
	Scene.Bag()->AddMaterial(Lens, 1);
	const int32 DayBefore = Scene.Run->Day();
	FCataclysmForgeSocketWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::ChangeSocketType(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), 1, ECataclysmSocketType::Utility, false, Done, Message);
	TestEqual(TEXT("changing the Viper's socket is refused"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying so"), Message, FString(TEXT("Unsocket the gem first.")));
	TestTrue(TEXT("the socket is still Attack with the Viper in it"),
			 Scene.Bag()->ItemAt(Cell)->Sockets[1].Type == ECataclysmSocketType::Attack
				 && Scene.Bag()->ItemAt(Cell)->Sockets[1].Gem == Viper);
	TestEqual(TEXT("the Focusing Lens is still carried"), Scene.Bag()->CountOfMaterial(Lens), 1);
	TestEqual(TEXT("and no day passed"), Scene.Run->Day(), DayBefore);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FS6. The screen's buttons: an addition below the maximum, and the two other types for each empty typed socket.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeSocketsScreenTest,
	"Cataclysm.ForgeSockets.TheScreenOffersAnAddBelowTheMaximumAndAChangeToEachOtherTypeOfAnEmptySocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeSocketsScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeSocketsTest;
	const auto Labels = [](const TArray<FCataclysmForgeSocketWorkAction>& Actions)
	{
		TArray<FString> Out;
		for (const FCataclysmForgeSocketWorkAction& Action : Actions)
		{
			Out.Add(Action.Label);
		}
		return FString::Join(Out, TEXT(" | "));
	};

	const TArray<FCataclysmForgeSocketWorkAction> One =
		UCataclysmForgeLayout::SocketWorkActionsFor(Helm(0.0f, {MakeSocket(ECataclysmSocketType::Defense)}), 2);
	const TArray<FCataclysmForgeSocketWorkAction> Full = UCataclysmForgeLayout::SocketWorkActionsFor(
		Helm(0.0f, {MakeSocket(ECataclysmSocketType::Defense), MakeSocket(ECataclysmSocketType::Attack, Viper)}), 2);
	const TArray<FCataclysmForgeSocketWorkAction> Old =
		UCataclysmForgeLayout::SocketWorkActionsFor(Helm(0.0f, {MakeSocket(ECataclysmSocketType::Any)}), 2);
	AddInfo(FString::Printf(TEXT("SOCKET WORK BUTTONS: one socket: %s; two, the second full: %s; one for any gem: %s"),
							*Labels(One), *Labels(Full), *Labels(Old)));

	TestEqual(TEXT("one Defense socket of 2: add, then the two other types"), Labels(One),
			  FString(TEXT("Add a socket (2 of 2) | Make socket 1 Attack | Make socket 1 Utility")));
	TestEqual(TEXT("at the maximum, with a gem in socket 2: no addition, no change for socket 2"), Labels(Full),
			  FString(TEXT("Make socket 1 Attack | Make socket 1 Utility")));
	TestEqual(TEXT("a socket for any gem: an addition only"), Labels(Old), FString(TEXT("Add a socket (2 of 2)")));

	FCataclysmForgeSocketWorkAction Pressed;
	TestTrue(TEXT("the add button is found"), UCataclysmForgeLayout::SocketWorkActionForButton(FName(TEXT("AddSocket")), One, Pressed)
												  && Pressed.bAdd);
	TestTrue(TEXT("and a change button carries its socket and type"),
			 UCataclysmForgeLayout::SocketWorkActionForButton(FName(TEXT("ChangeSocket_0_Utility")), One, Pressed)
				 && !Pressed.bAdd && Pressed.Socket == 0 && Pressed.NewType == ECataclysmSocketType::Utility);
	TestTrue(TEXT("no piece: nothing offered"),
			 UCataclysmForgeLayout::SocketWorkActionsFor(FCataclysmItem(), 2).IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
