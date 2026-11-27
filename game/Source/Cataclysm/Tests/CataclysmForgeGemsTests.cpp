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
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "DayClock/CataclysmDayClock.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

/**
 * THE FORGE'S SOCKET GEM AND UNSOCKET GEM. Issue #46, slice G4, ruled by the coordinating session on 2026-11-21.
 *
 * The design's operations rows: Socket Gem, a Jeweler's Setting Agent, 5 residue and 1 day; Unsocket Gem, Dismantling
 * Dust, 3 residue and 1 day; the residue's time penalty on both. No free removal, and an unsocketed gem comes back to
 * the bag. A gem fits a socket of its own type. The scene is the one `CataclysmForgeTests.cpp` builds, copied into this
 * file's namespace because the module is built in unity files.
 */
namespace CataclysmForgeGemsTest
{
	const FName Agent(UCataclysmForge::SettingAgentRow);
	const FName Dust(UCataclysmForge::DismantlingDustRow);
	const FName Life(TEXT("Gem_Of_Life"));
	const FName Viper(TEXT("Gem_Of_The_Viper"));
	const FName Rot(TEXT("Gem_Of_Rot"));

	FCataclysmSocket Socket(ECataclysmSocketType Type, FName Gem = NAME_None,
							ECataclysmRarity Rarity = ECataclysmRarity::Everyday)
	{
		FCataclysmSocket Out;
		Out.Type = Type;
		Out.Gem = Gem;
		Out.GemRarity = Rarity;
		return Out;
	}

	/** A helm carrying `Residue`: socket 1 Defense and empty, socket 2 Attack holding a Superb Of The Viper. */
	FCataclysmItem Helm(float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.Residue = Residue;
		Item.Sockets = {Socket(ECataclysmSocketType::Defense),
						Socket(ECataclysmSocketType::Attack, Viper, ECataclysmRarity::Superb)};
		return Item;
	}

	struct FForgeGemsScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && Character && Character->GetInventory() && Character->GetEquipment() && Run
				&& Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }
	};

	/** A world with this project's game instance, a possessed player and a run; nothing carried, nothing worn. */
	FForgeGemsScene MakeScene()
	{
		FForgeGemsScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);
		ACataclysmPlayerState* State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (State && Controller && Out.Character)
		{
			Controller->SetPlayerState(State);
			Controller->Possess(Out.Character);
			FCataclysmCharacterPartition Partition;
			Partition.Lethality = ECataclysmLethality::Standard;
			Partition.Population = ECataclysmPopulation::Offline;
			// SOLO SELF-FOUND, WITH GOLD OF ITS OWN, so every craft can pay (#2830) and no account record is written.
			Partition.bSoloSelfFound = true;
			State->SetIdentity(TEXT("Jeweler"), Partition);
			State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Out.Run->Begin(/* InSeed */ 2846, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
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
// FG1. The rules: what socketing and unsocketing take, and every refusal.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeGemsRulesTest,
	"Cataclysm.ForgeGems.SocketingTakesASettingAgent5ResidueAndADayAndUnsocketingDust3ResidueAndADay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeGemsRulesTest::RunTest(const FString&)
{
	using namespace CataclysmForgeGemsTest;
	const FCataclysmItem Item = Helm(87.0f);

	const FCataclysmForgeSocketing In =
		UCataclysmForge::SocketGemOf(Item, 0, Life, ECataclysmRarity::Superb, TEXT("Defense"), 2, 1, 1, 1000000);
	TestTrue(TEXT("a Defense gem goes into the empty Defense socket"), In.bPossible);
	TestEqual(TEXT("with a Setting Agent"), In.Material, Agent);
	TestEqual(TEXT("residue 87 becomes 92"), In.ResidueAfter, 92.0f, 0.001f);
	TestEqual(TEXT("one day below 100 residue"), In.Days, 1);
	TestEqual(TEXT("the first press's question"), UCataclysmForge::SocketingQuestion(In),
			  FString(TEXT("Press again to socket Of Life (Superb) into the Defense socket: one Jeweler's Setting Agent, "
						   "685 gold, residue 87 to 92, 1 day of the run.")));
	TestEqual(TEXT("150 residue: two days"),
			  UCataclysmForge::SocketGemOf(Helm(150.0f), 0, Life, ECataclysmRarity::Superb, TEXT("Defense"), 1, 1, 1, 1000000).Days, 2);

	TestEqual(TEXT("a filled socket is refused"),
			  UCataclysmForge::SocketGemOf(Item, 1, Rot, ECataclysmRarity::Everyday, TEXT("Attack"), 1, 1, 1, 1000000).Refusal,
			  FString(TEXT("That socket already holds Of The Viper; unsocket it first.")));
	TestEqual(TEXT("a gem of another type is refused"),
			  UCataclysmForge::SocketGemOf(Item, 0, Rot, ECataclysmRarity::Everyday, TEXT("Attack"), 1, 1, 1, 1000000).Refusal,
			  FString(TEXT("An Attack gem does not fit a Defense socket.")));
	TestEqual(TEXT("no such socket"),
			  UCataclysmForge::SocketGemOf(Item, 5, Life, ECataclysmRarity::Superb, TEXT("Defense"), 1, 1, 1, 1000000).Refusal,
			  FString(TEXT("That piece has no socket 6.")));
	TestEqual(TEXT("no such gem carried"),
			  UCataclysmForge::SocketGemOf(Item, 0, Life, ECataclysmRarity::Superb, TEXT("Defense"), 0, 1, 1, 1000000).Refusal,
			  FString(TEXT("No Of Life (Superb) is carried.")));
	TestEqual(TEXT("no Setting Agent"),
			  UCataclysmForge::SocketGemOf(Item, 0, Life, ECataclysmRarity::Superb, TEXT("Defense"), 1, 0, 1, 1000000).Refusal,
			  FString(TEXT("Socketing a gem takes a Jeweler's Setting Agent; 0 held.")));

	const FCataclysmForgeSocketing Out = UCataclysmForge::UnsocketGemOf(Item, 1, 1, true, 1, 1000000);
	TestTrue(TEXT("the Viper comes out with Dust"), Out.bPossible);
	TestTrue(TEXT("it is the Superb Of The Viper"), Out.Gem == Viper && Out.Rarity == ECataclysmRarity::Superb);
	TestEqual(TEXT("residue 87 becomes 90"), Out.ResidueAfter, 90.0f, 0.001f);
	TestEqual(TEXT("its question"), UCataclysmForge::SocketingQuestion(Out),
			  FString(TEXT("Press again to unsocket Of The Viper (Superb): one Dismantling Dust, 685 gold, residue 87 to 90, 1 day "
						   "of the run; the gem goes back to the bag.")));
	TestEqual(TEXT("an empty socket has nothing to take out"), UCataclysmForge::UnsocketGemOf(Item, 0, 1, true, 1, 1000000).Refusal,
			  FString(TEXT("That socket holds no gem.")));
	TestEqual(TEXT("no Dust: no removal"), UCataclysmForge::UnsocketGemOf(Item, 1, 0, true, 1, 1000000).Refusal,
			  FString(TEXT("Unsocketing a gem takes Dismantling Dust; 0 held.")));
	TestEqual(TEXT("no room for the gem"), UCataclysmForge::UnsocketGemOf(Item, 1, 1, false, 1, 1000000).Refusal,
			  FString(TEXT("The bag has no free cell for the gem.")));
	AddInfo(FString::Printf(TEXT("FORGE GEM RULES: %s"), *UCataclysmForge::SocketingQuestion(In)));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FG2. Socketing a carried piece: the gem and a Setting Agent leave the bag, the socket holds the gem, the residue
// rises by 5 and a day passes; a second socketing with no Agent left is refused and changes nothing.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeGemsSocketTest,
	"Cataclysm.ForgeGems.SocketingACarriedPiecePutsTheGemInTakesTheAgentAndPassesADay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeGemsSocketTest::RunTest(const FString&)
{
	using namespace CataclysmForgeGemsTest;
	FForgeGemsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Helm(87.0f));
	Scene.Bag()->AddGems(Life, ECataclysmRarity::Superb, 2);
	Scene.Bag()->AddMaterial(Agent, 1);
	const int32 DayBefore = Scene.Run->Day();

	FCataclysmForgeSocketing Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::SocketGem(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, Life, ECataclysmRarity::Superb, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the socketing is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	TestTrue(TEXT("socket 1 holds the Superb Of Life"),
			 After->Sockets[0].Gem == Life && After->Sockets[0].GemRarity == ECataclysmRarity::Superb);
	TestEqual(TEXT("residue 87 becomes 92"), After->Residue, 92.0f, 0.001f);
	TestEqual(TEXT("one Of Life is left"), Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Superb), 1);
	TestEqual(TEXT("the Setting Agent is used"), Scene.Bag()->CountOfMaterial(Agent), 0);
	TestEqual(TEXT("one day passed"), Scene.Run->Day(), DayBefore + 1);

	// THE SECOND OF LIFE, WITH NO AGENT LEFT: refused, and nothing moves.
	const ECataclysmForgeResult Refused = UCataclysmForgeCraft::SocketGem(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), 0, Life, ECataclysmRarity::Superb, false, Done, Message);
	TestEqual(TEXT("socketing into a filled socket is refused"), static_cast<int32>(Refused),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("one Of Life is still carried"), Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Superb), 1);
	TestEqual(TEXT("and no day passed"), Scene.Run->Day(), DayBefore + 1);
	AddInfo(FString::Printf(TEXT("SOCKETED: residue %.0f, day %d to %d; then '%s'"), After->Residue, DayBefore,
							Scene.Run->Day(), *Message));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FG3. Unsocketing a carried piece: one Dust is used, the gem is back in the bag, the socket is empty, the residue
// rises by 3 and a day passes; with no Dust it is refused and nothing moves.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeGemsUnsocketTest,
	"Cataclysm.ForgeGems.UnsocketingTakesDustAndGivesTheGemBackAndWithNoDustNothingMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeGemsUnsocketTest::RunTest(const FString&)
{
	using namespace CataclysmForgeGemsTest;
	FForgeGemsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Helm(87.0f));
	const int32 DayBefore = Scene.Run->Day();
	FCataclysmForgeSocketing Done;
	FString Message;

	// NO DUST: refused, and the Viper stays in its socket.
	const ECataclysmForgeResult Refused =
		UCataclysmForgeCraft::UnsocketGem(Scene.World, FCataclysmForgeTarget::Carried(Cell), 1, false, Done, Message);
	TestEqual(TEXT("with no Dust the gem cannot come out"), static_cast<int32>(Refused),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying so"), Message, FString(TEXT("Unsocketing a gem takes Dismantling Dust; 0 held.")));
	TestTrue(TEXT("the Viper is still socketed"), Scene.Bag()->ItemAt(Cell)->Sockets[1].Gem == Viper);
	TestEqual(TEXT("and no day passed"), Scene.Run->Day(), DayBefore);

	Scene.Bag()->AddMaterial(Dust, 1);
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::UnsocketGem(Scene.World, FCataclysmForgeTarget::Carried(Cell), 1, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("with Dust it is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	TestTrue(TEXT("socket 2 is empty"), After->Sockets[1].Gem.IsNone());
	TestEqual(TEXT("the Superb Of The Viper is back in the bag"), Scene.Bag()->CountOfGems(Viper, ECataclysmRarity::Superb),
			  1);
	TestEqual(TEXT("the Dust is used"), Scene.Bag()->CountOfMaterial(Dust), 0);
	TestEqual(TEXT("residue 87 becomes 90"), After->Residue, 90.0f, 0.001f);
	TestEqual(TEXT("one day passed"), Scene.Run->Day(), DayBefore + 1);
	AddInfo(FString::Printf(TEXT("UNSOCKETED: residue %.0f, day %d to %d"), After->Residue, DayBefore, Scene.Run->Day()));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FG4. The forge's screen offers, for a piece, each carried gem that fits each empty socket and the removal of each
// socketed gem, and lists the sockets.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeGemsScreenTest,
	"Cataclysm.ForgeGems.TheScreenOffersEachFittingGemForAnEmptySocketAndTheRemovalOfASocketedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeGemsScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeGemsTest;
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: a bag"), Bag))
	{
		return false;
	}
	Bag->AddGems(Life, ECataclysmRarity::Superb, 1); // Defense: fits socket 1
	Bag->AddGems(Rot, ECataclysmRarity::Everyday, 1); // Attack: fits no empty socket of this helm

	const TArray<FCataclysmForgeSocketAction> Actions = UCataclysmForgeLayout::SocketActionsFor(Helm(87.0f), Bag);
	TArray<FString> Labels;
	for (const FCataclysmForgeSocketAction& Action : Actions)
	{
		Labels.Add(Action.Label);
	}
	const TArray<FString> Expected = {TEXT("Socket Of Life (Superb) into socket 1 (Defense)"),
									  TEXT("Unsocket Of The Viper (Superb) from socket 2")};
	TestEqual(TEXT("two actions"), Labels.Num(), Expected.Num());
	for (int32 Index = 0; Index < FMath::Min(Labels.Num(), Expected.Num()); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("action %d"), Index + 1), Labels[Index], Expected[Index]);
	}
	FCataclysmForgeSocketAction Found;
	TestTrue(TEXT("each action is found again by its button"),
			 Actions.Num() == 2 && UCataclysmForgeLayout::SocketActionForButton(Actions[1].ButtonName, Actions, Found)
				 && Found.bUnsocket && Found.Socket == 1);
	TestEqual(TEXT("the sockets line"), UCataclysmForgeLayout::SocketsDetailFor(Helm(87.0f)),
			  FString(TEXT("Sockets: Defense socket: empty; Attack socket: Of The Viper (Superb)")));
	AddInfo(FString::Printf(TEXT("FORGE SOCKET ACTIONS: %s"), *FString::Join(Labels, TEXT(" | "))));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
