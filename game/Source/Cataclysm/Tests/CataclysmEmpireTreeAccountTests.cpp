// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The empire upgrade tree on the account record, and buying, choosing and respec through the game instance. Issue
 * #2741, slice 1b. The rules themselves are slice 1a's, tested in `CataclysmEmpireTreeTests.cpp`; these test where
 * the tree is kept, how it is read, and that every change to it is saved.
 *
 * THE SLOT IS THE TEST'S OWN. While an automation test runs, `UCataclysmGameInstance::AccountSlotFor` ends an account
 * slot in `_AutomationTest`, so nothing here touches the player's own tree; each scene deletes that slot first, and
 * `FCataclysmTestSaves` deletes it again when the test ends. Gold's tests (`CataclysmGoldTests.cpp`) build their
 * scenes the same way.
 *
 * WHAT IS NOT HERE: a Solo Self-Found character's own tree (slice 1c), the screen (slice 2), and what a bought node
 * does (slices 3 to 5).
 */
namespace CataclysmEmpireTreeAccountTest
{
	// The tree file's identifiers, as slice 1a's tests name them.
	const FName StructuralIntegrity(TEXT("basic_0000000w"));	// Architect, a root, 10 points
	const FName FortifiedGates(TEXT("basic_arch_t1d"));			// <- Structural Integrity at 2, 8 points
	const FName MasonryTechniques(TEXT("basic_00000010"));		// <- Fortified Gates at 6, 15 points
	const FName MerchantGuild(TEXT("basic_tres_t1e"));		// Treasury, <- Coinage at 2; waits on a market
	// (Rough Draft, an Artisan root, was the node used here until #2741's crafting slice C4 built it, 2026-11-25.)
	const FName Foundations(TEXT("capstone_0000000m"));			// the centre, at 25: Aegis of Hope, The Delver, The Hoarder

	/** A world with this project's game instance and a possessed player of one partition. */
	struct FTreeScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State && Character; }

		/** The scene's account record: the one the game instance holds for its partition. */
		UCataclysmAccountSave* Account() const { return Instance != nullptr ? Instance->AccountFor(Partition) : nullptr; }
	};

	/** Waits for any write to the slot to finish, giving up after two seconds. */
	void WaitForTheSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}

	/** Waits for any write to the slot to finish, then deletes it when it exists. */
	void DeleteSlot(const FString& SlotName)
	{
		WaitForTheSlot(SlotName);
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UCataclysmSaveWriter::UserIndex);
		}
	}

	/**
	 * The scene: a partition of the lethality mode given, offline, the test's account slot emptied first. `bWriting`
	 * begins the save writer, as play does, so `AccountChanged` writes the account record and `Clock` the run record.
	 * `bWithARun` begins a run at the lethality mode's rung, as a character's run is begun.
	 */
	FTreeScene MakeScene(ECataclysmLethality Lethality, bool bSoloSelfFound, bool bWriting, bool bWithARun)
	{
		FTreeScene Out;
		Out.Partition.Lethality = Lethality;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

		const FString Slot = UCataclysmGameInstance::AccountSlotFor(Out.Partition);
		if (!Slot.IsEmpty())
		{
			DeleteSlot(Slot);
		}

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
			Out.State->SetIdentity(TEXT("Treewright"), Out.Partition);
		}

		if (bWithARun)
		{
			Out.Instance->BeginEmpireRun(/*Seed*/ 2741, ECataclysmSurgeMode::Static, static_cast<int32>(Lethality),
										 /*DifficultyTier*/ 1);
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

	/** The account record in a slot, read as the next session would read it, or null. */
	const UCataclysmAccountSave* ReadTheSlot(const FString& Slot, ECataclysmSaveLoadResult& OutResult)
	{
		WaitForTheSlot(Slot);
		OutResult = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString Message;
		return Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::ReadFromSlot(
			Slot, UCataclysmSaveWriter::UserIndex, UCataclysmAccountSave::StaticClass(), GetTransientPackage(),
			OutResult, Message));
	}

	/** An allocation as "node points/option, ..." for a log line. */
	FString Describe(const FCataclysmPassiveAllocation& Allocation)
	{
		TArray<FString> Parts;
		for (const FCataclysmSpentNode& Spent : Allocation.Nodes)
		{
			Parts.Add(FString::Printf(TEXT("%s %d/%d"), *Spent.Node.ToString(), Spent.Points, Spent.ChosenOption));
		}
		return Parts.IsEmpty() ? FString(TEXT("nothing")) : FString::Join(Parts, TEXT(", "));
	}

	/** Whether two allocations hold the same points and the same choices in every node, whatever their order. */
	bool SameTree(const FCataclysmPassiveAllocation& Left, const FCataclysmPassiveAllocation& Right)
	{
		if (Left.Total() != Right.Total() || Left.Nodes.Num() != Right.Nodes.Num())
		{
			return false;
		}
		for (const FCataclysmSpentNode& Spent : Left.Nodes)
		{
			if (Right.PointsIn(Spent.Node) != Spent.Points || Right.ChosenOptionIn(Spent.Node) != Spent.ChosenOption)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * An allocation bought again from nothing through the buying rules, with a bank that never runs short: each pass
	 * chooses every option the target took and buys every point it holds that the rules allow, and the passes stop when
	 * one buys nothing. What comes back equals the target exactly when play could have bought the target.
	 */
	FCataclysmPassiveAllocation Replay(const UDataTable* Nodes, const UDataTable* Edges,
									   const FCataclysmPassiveAllocation& Target)
	{
		FCataclysmPassiveAllocation Replayed;
		double Bank = 10000.0; // A test's: more points than the whole tree holds, so the bank never refuses.
		bool bBoughtAnything = true;
		while (bBoughtAnything)
		{
			bBoughtAnything = false;
			for (const FCataclysmSpentNode& Spent : Target.Nodes)
			{
				FString Reason;
				if (Spent.ChosenOption > 0 && Replayed.ChosenOptionIn(Spent.Node) == 0
					&& UCataclysmEmpireTree::ChooseOption(Nodes, Replayed, Spent.Node, Spent.ChosenOption, Reason))
				{
					bBoughtAnything = true;
				}
				while (Replayed.PointsIn(Spent.Node) < Spent.Points
					   && UCataclysmEmpireTree::Spend(Nodes, Edges, Replayed, Spent.Node, Bank, Reason))
				{
					bBoughtAnything = true;
				}
			}
		}
		return Replayed;
	}

	/** The committed account file's tree, put into an allocation by hand: 25 Architect points and the capstone. */
	FCataclysmPassiveAllocation TheFilesTree()
	{
		FCataclysmPassiveAllocation Tree;
		Tree.Add(StructuralIntegrity, 10);
		Tree.Add(FortifiedGates, 8);
		Tree.Add(MasonryTechniques, 7);
		Tree.SetChosenOption(Foundations, 2);
		Tree.Add(Foundations, 1);
		return Tree;
	}
}

// NO `using namespace` AT FILE SCOPE, for the reason `CataclysmEmpireTreeTests.cpp` gives: in a unity build it would
// reach the files compiled after this one. Each test opens the namespace inside its body.

// ---------------------------------------------------------------------------
// The account record holds the tree
// ---------------------------------------------------------------------------

/**
 * `Account_v8.json`, the committed account file since #47 slice F6a (`Account_v7.json` from issue #2741 slice 9), reads its tree into the record: three Architect nodes at 10, 8 and 7,
 * and the first centre capstone with its second option chosen and its point spent; read rather than migrated, beside
 * the bank and the gold it held at version 3. Prints "ACCOUNT FILE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountFileTest,
	"Cataclysm.EmpireTree.TheCommittedAccountFileReadsItsTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountFileTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeAccountTest;

	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v8.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Account_v8.json would not load: %s -- %s"), FCataclysmSaveStorage::Describe(Result),
								 *Message));
		return false;
	}

	AddInfo(FString::Printf(TEXT("ACCOUNT FILE: %s, version %d, %g points banked, %lld gold, the tree %s (%d spent)"),
							FCataclysmSaveStorage::Describe(Result), Read->SchemaVersion, Read->EmpireUpgradePoints,
							Read->Gold, *Describe(Read->EmpireTree), Read->EmpireTree.Total()));

	TestEqual(TEXT("it is read rather than migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("at the current version"), Read->SchemaVersion, UCataclysmAccountSave::SchemaVersionNow);
	TestEqual(TEXT("its tree holds four nodes"), Read->EmpireTree.Nodes.Num(), 4);
	TestEqual(TEXT("Structural Integrity at 10"), Read->EmpireTree.PointsIn(StructuralIntegrity), 10);
	TestEqual(TEXT("Fortified Gates at 8"), Read->EmpireTree.PointsIn(FortifiedGates), 8);
	TestEqual(TEXT("Masonry Techniques at 7"), Read->EmpireTree.PointsIn(MasonryTechniques), 7);
	TestEqual(TEXT("Foundations of the Empire at 1"), Read->EmpireTree.PointsIn(Foundations), 1);
	TestEqual(TEXT("with its second option chosen"), Read->EmpireTree.ChosenOptionIn(Foundations), 2);
	TestEqual(TEXT("26 points spent in all"), Read->EmpireTree.Total(), 26);
	TestTrue(TEXT("and the bank beside it keeps its 37.625"), Read->EmpireUpgradePoints == 37.625);
	TestEqual(TEXT("and the gold its 1234567"), Read->Gold, static_cast<int64>(1234567));
	return true;
}

/**
 * `Migrate_3_to_4` writes an empty tree into an account written at 3, keeps an object already there, and refuses
 * anything else under the key: run on the committed version 3 file as it is, with a tree of one node added, and with a
 * word where the tree goes. And `Account_v3.json` reads through the chain into a record at the current version with an
 * empty tree and every other field kept. Prints "ACCOUNT MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountMigrationTest,
	"Cataclysm.EmpireTree.AnAccountFromBeforeTheTreeMigratesHoldingAnEmptyTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountMigrationTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v3.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	// THREE COPIES AT VERSION 3: with no tree, with a tree of one node, and with a word where the tree goes.
	const TSharedPtr<FJsonObject> NoTree = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> Held = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> Word = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Account_v3.json is a JSON object"), NoTree.IsValid() && Held.IsValid() && Word.IsValid())
		|| !TestFalse(TEXT("set-up: and holds no tree"), NoTree->HasField(TEXT("EmpireTree"))))
	{
		return false;
	}
	const TSharedRef<FJsonObject> OneNode = MakeShared<FJsonObject>();
	OneNode->SetStringField(TEXT("Node"), TEXT("basic_0000000w"));
	OneNode->SetNumberField(TEXT("Points"), 2);
	OneNode->SetNumberField(TEXT("ChosenOption"), 0);
	TArray<TSharedPtr<FJsonValue>> OneEntry;
	OneEntry.Add(MakeShared<FJsonValueObject>(OneNode));
	const TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("Nodes"), OneEntry);
	Held->SetObjectField(TEXT("EmpireTree"), Tree);
	Word->SetStringField(TEXT("EmpireTree"), TEXT("everything"));

	const TArrayView<const FCataclysmSaveMigrationStep> Steps = GetDefault<UCataclysmAccountSave>()->MigrationSteps();
	FString NoTreeMessage;
	FString HeldMessage;
	FString WordMessage;
	const ECataclysmSaveMigrationResult NoTreeResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmAccountSave::SchemaVersionNow, NoTree.ToSharedRef(), NoTreeMessage);
	const ECataclysmSaveMigrationResult HeldResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmAccountSave::SchemaVersionNow, Held.ToSharedRef(), HeldMessage);
	const ECataclysmSaveMigrationResult WordResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmAccountSave::SchemaVersionNow, Word.ToSharedRef(), WordMessage);

	const TSharedPtr<FJsonObject>* Written = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* WrittenNodes = nullptr;
	const bool bWritten = NoTree->TryGetObjectField(TEXT("EmpireTree"), Written) && Written != nullptr
		&& Written->IsValid() && (*Written)->TryGetArrayField(TEXT("Nodes"), WrittenNodes) && WrittenNodes != nullptr;
	const TSharedPtr<FJsonObject>* Kept = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* KeptNodes = nullptr;
	const bool bKept = Held->TryGetObjectField(TEXT("EmpireTree"), Kept) && Kept != nullptr && Kept->IsValid()
		&& (*Kept)->TryGetArrayField(TEXT("Nodes"), KeptNodes) && KeptNodes != nullptr;

	AddInfo(FString::Printf(TEXT("ACCOUNT MIGRATED: no tree %d (%s) -> %s; one node %d (%s) -> %d node(s); a word %d (%s)"),
							static_cast<int32>(NoTreeResult), *NoTreeMessage,
							bWritten ? *FString::Printf(TEXT("a tree of %d node(s)"), WrittenNodes->Num())
									 : TEXT("NO TREE"),
							static_cast<int32>(HeldResult), *HeldMessage, bKept ? KeptNodes->Num() : -1,
							static_cast<int32>(WordResult), *WordMessage));

	TestEqual(TEXT("an account at 3 with no tree migrates"), static_cast<int32>(NoTreeResult),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("holding an empty tree, written out"), bWritten && WrittenNodes->Num() == 0);
	TestTrue(TEXT("one already holding a tree of one node keeps it"), bKept && KeptNodes->Num() == 1);
	TestNotEqual(TEXT("and one holding a word is refused"), static_cast<int32>(WordResult),
				 static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));

	// THE FILE FROM BEFORE THE TREE, through the chain, compared with what the record writes back.
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Account_v3.json would not load: %s -- %s"), FCataclysmSaveStorage::Describe(Result),
								 *Message));
		return false;
	}
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmAccountSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheAccountsVersion4Defaults(*Expected);
	CataclysmSaveFixtures::AddTheAccountsVersion5Defaults(*Expected); // And 5, #2741 slice 6e: no debt.
	CataclysmSaveFixtures::AddTheAccountsVersion6Defaults(*Expected); // And 6, #2741 slice 8: no extra option.
	CataclysmSaveFixtures::AddTheAccountsVersion7Defaults(*Expected);
	CataclysmSaveFixtures::AddTheAccountsVersion8Defaults(*Expected); // And #47 slice F6a's: the forge count.
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);
	AddInfo(FString::Printf(TEXT("ACCOUNT MIGRATED: Account_v3.json %s, at version %d, a tree of %d node(s); the rest %s"),
							FCataclysmSaveStorage::Describe(Result), Read->SchemaVersion, Read->EmpireTree.Nodes.Num(),
							bSame ? TEXT("kept") : *Where));
	TestEqual(TEXT("Account_v3.json is migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("to the current version"), Read->SchemaVersion, UCataclysmAccountSave::SchemaVersionNow);
	TestEqual(TEXT("holding an empty tree"), Read->EmpireTree.Nodes.Num(), 0);
	TestTrue(TEXT("keeping its 37.625 empire points"), Read->EmpireUpgradePoints == 37.625);
	TestEqual(TEXT("and its gold"), Read->Gold, static_cast<int64>(1234567));
	TestTrue(TEXT("and every other field"), bSame);
	return true;
}

/**
 * The committed account file's tree is one play can reach: bought again from nothing through the buying rules, it
 * comes back exactly. And the replay can say no: Masonry Techniques at 7 with nothing leading to it buys nothing.
 * Prints "REPLAY".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountFileReachableTest,
	"Cataclysm.EmpireTree.TheCommittedAccountFilesTreeIsOnePlayCanReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountFileReachableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeAccountTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	FString Text;
	FString Reason;
	if (!TestTrue(TEXT("set-up: the tree's two tables load"), Nodes != nullptr && Edges != nullptr)
		|| !CataclysmSaveFixtures::Read(TEXT("Account_v8.json"), Text, Reason))
	{
		if (!Reason.IsEmpty())
		{
			AddError(Reason);
		}
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (!TestNotNull(TEXT("set-up: Account_v8.json loads"), Read))
	{
		return false;
	}

	const FCataclysmPassiveAllocation Replayed = Replay(Nodes, Edges, Read->EmpireTree);
	FCataclysmPassiveAllocation Unreachable;
	Unreachable.Add(MasonryTechniques, 7);
	const FCataclysmPassiveAllocation ReplayedUnreachable = Replay(Nodes, Edges, Unreachable);

	AddInfo(FString::Printf(TEXT("REPLAY: the file %s; bought again %s. Masonry Techniques alone: bought again %s"),
							*Describe(Read->EmpireTree), *Describe(Replayed), *Describe(ReplayedUnreachable)));

	TestTrue(TEXT("CONTROL: the file's tree is not empty"), Read->EmpireTree.Total() > 0);
	TestTrue(TEXT("the file's tree comes back exactly from the buying rules"), SameTree(Replayed, Read->EmpireTree));
	TestTrue(TEXT("and it is the tree this file is meant to hold"), SameTree(Read->EmpireTree, TheFilesTree()));
	TestFalse(TEXT("CONTROL: a tree play cannot reach does not come back"), SameTree(ReplayedUnreachable, Unreachable));
	TestEqual(TEXT("it buys nothing at all"), ReplayedUnreachable.Total(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// The one place that says which tree a character has
// ---------------------------------------------------------------------------

/**
 * `EmpireTreeHeldBy` reads the account record of the lethality mode playing: its bank and its tree, the very fields.
 * A Solo Self-Found character's are its own, on its player state, and a point bought through the game instance goes
 * there; a world with nobody playing has none, and says so. Prints "HELD".
 *
 * NAMED `TheTreeHeldIsTheLethalityModesAccounts` IN SLICE 1B, when a Solo Self-Found character had none and was told it
 * was slice 1c's; renamed in slice 1c (issue #2741), which gave it its own.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountHeldTest,
	"Cataclysm.EmpireTree.TheTreeHeldIsTheModesAccountsOrASoloSelfFoundCharactersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountHeldTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeAccountTest;

	FTreeScene Hardcore = MakeScene(ECataclysmLethality::Hardcore, /*bSoloSelfFound*/ false, /*bWriting*/ false,
									/*bWithARun*/ false);
	FTreeScene Solo = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound*/ true, /*bWriting*/ false,
								/*bWithARun*/ false);
	UWorld* Nobody = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Hardcore.World, Solo.World, Nobody })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the two scenes were built"), Hardcore.IsUsable() && Solo.IsUsable())
		|| !TestNotNull(TEXT("set-up: and a world with nobody in it"), Nobody))
	{
		return false;
	}
	Nobody->SetGameInstance(NewObject<UCataclysmGameInstance>());

	UCataclysmAccountSave* Account = Hardcore.Account();
	if (!TestNotNull(TEXT("set-up: the Hardcore account record"), Account))
	{
		return false;
	}

	FCataclysmHeldEmpireTree HardcoreTree;
	FCataclysmHeldEmpireTree SoloTree;
	FCataclysmHeldEmpireTree NobodysTree;
	const bool bHardcore = UCataclysmGameInstance::EmpireTreeHeldBy(Hardcore.World, HardcoreTree);
	const bool bSolo = UCataclysmGameInstance::EmpireTreeHeldBy(Solo.World, SoloTree);
	const bool bNobody = UCataclysmGameInstance::EmpireTreeHeldBy(Nobody, NobodysTree);

	// A SOLO SELF-FOUND CHARACTER'S OWN BANK PAYS FOR ITS OWN POINT, and no account record is made for it.
	Solo.State->SetOwnEmpireTree(1.0, FCataclysmPassiveAllocation());
	FString SoloBuys;
	const bool bSoloBought = UCataclysmGameInstance::BuyEmpireTreePoint(Solo.World, StructuralIntegrity, SoloBuys);

	AddInfo(FString::Printf(TEXT("HELD: Hardcore %s, the account's own fields %s, its own %s; Solo Self-Found %s, the "
								 "player state's own fields %s, its own %s; nobody %s, \"%s\"; a Solo Self-Found buy %s "
								 "(%s), its own tree %s, its own bank %g, an account held %s"),
							bHardcore ? TEXT("yes") : TEXT("no"),
							HardcoreTree.Bank == &Account->EmpireUpgradePoints
									&& HardcoreTree.Allocation == &Account->EmpireTree
								? TEXT("yes")
								: TEXT("NO"),
							HardcoreTree.bTheCharactersOwn ? TEXT("yes") : TEXT("no"),
							bSolo ? TEXT("yes") : TEXT("no"),
							SoloTree.Bank == &Solo.State->EditOwnEmpireUpgradePoints()
									&& SoloTree.Allocation == &Solo.State->EditOwnEmpireTree()
								? TEXT("yes")
								: TEXT("NO"),
							SoloTree.bTheCharactersOwn ? TEXT("yes") : TEXT("no"), bNobody ? TEXT("yes") : TEXT("no"),
							*NobodysTree.Why, bSoloBought ? TEXT("bought") : TEXT("refused"), *SoloBuys,
							*Describe(Solo.State->GetOwnEmpireTree()), Solo.State->GetOwnEmpireUpgradePoints(),
							Solo.Instance->HeldAccount != nullptr ? TEXT("yes") : TEXT("no")));

	TestTrue(TEXT("a Hardcore character has a tree"), bHardcore && HardcoreTree.IsHeld());
	TestTrue(TEXT("whose bank is its account record's"), HardcoreTree.Bank == &Account->EmpireUpgradePoints);
	TestTrue(TEXT("and whose tree is its account record's"), HardcoreTree.Allocation == &Account->EmpireTree);
	TestFalse(TEXT("and not its own"), HardcoreTree.bTheCharactersOwn);
	TestTrue(TEXT("with no reason given"), HardcoreTree.Why.IsEmpty());
	TestTrue(TEXT("a Solo Self-Found character has a tree"), bSolo && SoloTree.IsHeld());
	TestTrue(TEXT("whose bank is its player state's own"), SoloTree.Bank == &Solo.State->EditOwnEmpireUpgradePoints());
	TestTrue(TEXT("and whose tree is its player state's own"), SoloTree.Allocation == &Solo.State->EditOwnEmpireTree());
	TestTrue(TEXT("and says so"), SoloTree.bTheCharactersOwn);
	TestFalse(TEXT("a world with nobody playing has none"), bNobody || NobodysTree.IsHeld());
	TestFalse(TEXT("and says why"), NobodysTree.Why.IsEmpty());
	TestTrue(TEXT("a Solo Self-Found character's point is bought"), bSoloBought);
	TestEqual(TEXT("into its own tree"), Solo.State->GetOwnEmpireTree().PointsIn(StructuralIntegrity), 1);
	TestTrue(TEXT("paid from its own bank"), Solo.State->GetOwnEmpireUpgradePoints() == 0.0);
	TestTrue(TEXT("and no account record is held for it"), Solo.Instance->HeldAccount == nullptr);
	return true;
}

// ---------------------------------------------------------------------------
// Buying and choosing, saved
// ---------------------------------------------------------------------------

/**
 * A point bought through the game instance comes off the bank and into the tree, and the account record is written:
 * its slot reads back the same tree and bank, and so does a new game instance. A refused buy -- Merchant Guild, which
 * waits on a market -- changes nothing and writes nothing. Prints "BUY".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountBuyTest,
	"Cataclysm.EmpireTree.APointBoughtComesOffTheBankAndTheAccountIsWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountBuyTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeAccountTest;

	FTreeScene Scene = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound*/ false, /*bWriting*/ true,
								 /*bWithARun*/ false);
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

	UCataclysmAccountSave* Account = Scene.Account();
	if (!TestNotNull(TEXT("set-up: the account record"), Account))
	{
		return false;
	}
	Account->EmpireUpgradePoints = 3.5;
	const FString Slot = UCataclysmGameInstance::AccountSlotFor(Scene.Partition);

	const int32 WritesBefore = Writer->WritesStarted();
	FString Reason;
	const bool bBought = UCataclysmGameInstance::BuyEmpireTreePoint(Scene.World, StructuralIntegrity, Reason);
	const int32 WritesForTheBuy = Writer->WritesStarted() - WritesBefore;

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	const UCataclysmAccountSave* Read = ReadTheSlot(Slot, Result);
	const UCataclysmAccountSave* NextSession = NewObject<UCataclysmGameInstance>()->AccountFor(Scene.Partition);

	// A REFUSED BUY, after: nothing changes and nothing is written.
	const int32 WritesBeforeTheRefusal = Writer->WritesStarted();
	FString Refusal;
	const bool bRefusedBought = UCataclysmGameInstance::BuyEmpireTreePoint(Scene.World, MerchantGuild, Refusal);
	const int32 WritesForTheRefusal = Writer->WritesStarted() - WritesBeforeTheRefusal;

	AddInfo(FString::Printf(TEXT("BUY: %s (%s) with %d write(s); the tree %s, %g banked; the slot %s (%s) holds %s, %g; "
								 "a new game instance %s. Merchant Guild %s: \"%s\", %d write(s); the tree %s, %g banked"),
							bBought ? TEXT("bought") : TEXT("refused"), *Reason, WritesForTheBuy,
							*Describe(Account->EmpireTree), Account->EmpireUpgradePoints,
							Read != nullptr ? TEXT("read") : TEXT("did not read"),
							FCataclysmSaveStorage::Describe(Result),
							Read != nullptr ? *Describe(Read->EmpireTree) : TEXT("-"),
							Read != nullptr ? Read->EmpireUpgradePoints : -1.0,
							NextSession != nullptr ? *Describe(NextSession->EmpireTree) : TEXT("-"),
							bRefusedBought ? TEXT("bought") : TEXT("refused"), *Refusal, WritesForTheRefusal,
							*Describe(Account->EmpireTree), Account->EmpireUpgradePoints));

	TestEqual(TEXT("CONTROL: the slot is the test's own, not the player's"),
			  Slot, UCataclysmSavePartition::AccountSlotName(Scene.Partition) + TEXT("_AutomationTest"));
	TestTrue(TEXT("Structural Integrity is bought"), bBought);
	TestTrue(TEXT("with no reason given"), Reason.IsEmpty());
	TestEqual(TEXT("it holds a point"), Account->EmpireTree.PointsIn(StructuralIntegrity), 1);
	TestTrue(TEXT("paid from the bank, 3.5 to 2.5"), Account->EmpireUpgradePoints == 2.5);
	TestEqual(TEXT("and the account record's write started"), WritesForTheBuy, 1);
	if (!TestNotNull(TEXT("the account record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("holding the point"), Read->EmpireTree.PointsIn(StructuralIntegrity), 1);
	TestTrue(TEXT("and the bank it was paid from"), Read->EmpireUpgradePoints == 2.5);
	TestTrue(TEXT("and a new game instance reads the same tree"),
			 NextSession != nullptr && NextSession->EmpireTree.PointsIn(StructuralIntegrity) == 1);
	TestFalse(TEXT("Merchant Guild is refused"), bRefusedBought);
	TestTrue(TEXT("because it waits on a market"), Refusal.Contains(TEXT("a market")));
	TestEqual(TEXT("which writes nothing"), WritesForTheRefusal, 0);
	TestEqual(TEXT("and changes nothing in the tree"), Account->EmpireTree.Total(), 1);
	TestTrue(TEXT("or the bank"), Account->EmpireUpgradePoints == 2.5);
	return true;
}

/**
 * A centre capstone's option chosen through the game instance is saved with the tree: with 25 Architect points held,
 * Foundations of the Empire takes its second option and then its point, and the slot reads both back. A second choice
 * is refused, and the first stands. Prints "CHOOSE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountChooseTest,
	"Cataclysm.EmpireTree.ACapstoneOptionChosenIsSavedWithTheTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountChooseTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeAccountTest;

	FTreeScene Scene = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound*/ false, /*bWriting*/ true,
								 /*bWithARun*/ false);
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

	UCataclysmAccountSave* Account = Scene.Account();
	if (!TestNotNull(TEXT("set-up: the account record"), Account))
	{
		return false;
	}
	// THE 25 POINTS THE CAPSTONE OPENS AT, the committed file's path to it, put in by hand: their buying is not the
	// subject here.
	Account->EmpireTree.Add(StructuralIntegrity, 10);
	Account->EmpireTree.Add(FortifiedGates, 8);
	Account->EmpireTree.Add(MasonryTechniques, 7);
	Account->EmpireUpgradePoints = 2.0;
	const FString Slot = UCataclysmGameInstance::AccountSlotFor(Scene.Partition);

	const int32 WritesBefore = Writer->WritesStarted();
	FString ChoiceReason;
	const bool bChosen = UCataclysmGameInstance::ChooseEmpireTreeOption(Scene.World, Foundations, 2, ChoiceReason);
	const int32 WritesForTheChoice = Writer->WritesStarted() - WritesBefore;
	FString BuyReason;
	const bool bBought = UCataclysmGameInstance::BuyEmpireTreePoint(Scene.World, Foundations, BuyReason);
	FString SecondReason;
	const bool bSecondChosen = UCataclysmGameInstance::ChooseEmpireTreeOption(Scene.World, Foundations, 1, SecondReason);

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	const UCataclysmAccountSave* Read = ReadTheSlot(Slot, Result);

	AddInfo(FString::Printf(TEXT("CHOOSE: option 2 %s (%s) with %d write(s); its point %s (%s); option 1 after %s, "
								 "\"%s\"; the tree %s, %g banked; the slot holds %s, %g"),
							bChosen ? TEXT("chosen") : TEXT("refused"), *ChoiceReason, WritesForTheChoice,
							bBought ? TEXT("bought") : TEXT("refused"), *BuyReason,
							bSecondChosen ? TEXT("chosen") : TEXT("refused"), *SecondReason,
							*Describe(Account->EmpireTree), Account->EmpireUpgradePoints,
							Read != nullptr ? *Describe(Read->EmpireTree) : TEXT("-"),
							Read != nullptr ? Read->EmpireUpgradePoints : -1.0));

	TestTrue(TEXT("the second option is chosen"), bChosen);
	TestEqual(TEXT("and the choice writes the account record"), WritesForTheChoice, 1);
	TestTrue(TEXT("then the capstone's point is bought"), bBought);
	TestFalse(TEXT("a second choice is refused"), bSecondChosen);
	TestEqual(TEXT("and the first stands"), Account->EmpireTree.ChosenOptionIn(Foundations), 2);
	if (!TestNotNull(TEXT("the account record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("holding the choice"), Read->EmpireTree.ChosenOptionIn(Foundations), 2);
	TestEqual(TEXT("and the capstone's point"), Read->EmpireTree.PointsIn(Foundations), 1);
	TestTrue(TEXT("and the bank after it, 1"), Read->EmpireUpgradePoints == 1.0);
	TestTrue(TEXT("the whole tree as the committed file holds it"), SameTree(Read->EmpireTree, TheFilesTree()));
	return true;
}

// ---------------------------------------------------------------------------
// Respec
// ---------------------------------------------------------------------------

/**
 * A respec through the game instance returns every point to the bank, forgets every choice, and takes the mode's death
 * cost in days off the run -- 5 for Standard, 15 for Heretic -- and both records are written: the account record's slot
 * reads back the empty tree and the full bank, and the run record's the day the respec reached.
 *
 * THE RUN RECORD IS WRITTEN FIRST, IN THE SAME FRAME, BY THE CLOCK, before the respec: the save writer writes the run
 * record at most once a frame unless the trigger is one that writes whatever else wrote it, and the respec's must be,
 * or its days reach the disk only at the next write. The coordinating session's ruling of 2026-11-18. Prints "RESPEC".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountRespecTest,
	"Cataclysm.EmpireTree.ARespecReturnsThePointsAndTakesTheModesDaysOffTheRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountRespecTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeAccountTest;

	struct FMode
	{
		ECataclysmLethality Lethality;
		int32 Days;
		const TCHAR* Name;
	};
	const FMode Modes[] = {
		{ ECataclysmLethality::Standard, 5, TEXT("Standard") },	// A test's: the design's death cost for Standard.
		{ ECataclysmLethality::Heretic, 15, TEXT("Heretic") },		// A test's: and for Heretic.
	};

	for (const FMode& Mode : Modes)
	{
		FTreeScene Scene = MakeScene(Mode.Lethality, /*bSoloSelfFound*/ false, /*bWriting*/ true, /*bWithARun*/ true);
		UCataclysmSaveWriter* Writer = Scene.World ? UCataclysmSaveWriter::In(Scene.World) : nullptr;
		UCataclysmEmpireRun* Run = Scene.World ? UCataclysmGameInstance::EmpireRunFor(Scene.World, false) : nullptr;
		UCataclysmAccountSave* Account = Scene.Account();
		if (!TestTrue(*FString::Printf(TEXT("set-up: the %s scene, its writer, its run and its account"), Mode.Name),
					  Scene.IsUsable() && Writer != nullptr && Writer->IsWriting() && Run != nullptr
						  && Account != nullptr))
		{
			if (Scene.World)
			{
				Scene.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Account->EmpireTree = TheFilesTree();
		Account->EmpireUpgradePoints = 0.5;
		const FString Slot = UCataclysmGameInstance::AccountSlotFor(Scene.Partition);
		const int32 DayBefore = Run->Day();

		// THE RUN RECORD WRITTEN EARLIER IN THIS FRAME, before the respec, as the clock's interval write could be.
		const bool bWrittenFirst = Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);

		const int32 WritesBefore = Writer->WritesStarted();
		int32 Returned = 0;
		int32 Days = 0;
		FString Reason;
		const bool bRespecced = UCataclysmGameInstance::RespecEmpireTree(Scene.World, Returned, Days, Reason);
		const int32 Writes = Writer->WritesStarted() - WritesBefore;

		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
		const UCataclysmAccountSave* Read = ReadTheSlot(Slot, Result);
		WaitForTheSlot(Writer->RunSlotName());
		ECataclysmSaveLoadResult RunResult = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString RunMessage;
		const UCataclysmRunSave* RunRead = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::ReadFromSlot(
			Writer->RunSlotName(), UCataclysmSaveWriter::UserIndex, UCataclysmRunSave::StaticClass(),
			GetTransientPackage(), RunResult, RunMessage));

		AddInfo(FString::Printf(TEXT("RESPEC %s: the run written first this frame %s; %s (%s), %d point(s) returned, %d "
									 "day(s), day %d to %d, the run's death cost %d, %d write(s); the tree %s, %g "
									 "banked; the account slot %s, %g; the run slot's day %d"),
								Mode.Name, bWrittenFirst ? TEXT("yes") : TEXT("NO"),
								bRespecced ? TEXT("done") : TEXT("refused"), *Reason, Returned, Days,
								DayBefore, Run->Day(), Run->DeathDayCost(), Writes, *Describe(Account->EmpireTree),
								Account->EmpireUpgradePoints, Read != nullptr ? *Describe(Read->EmpireTree) : TEXT("-"),
								Read != nullptr ? Read->EmpireUpgradePoints : -1.0,
								RunRead != nullptr ? RunRead->Day : -1));

		const FString Where = Mode.Name;
		TestTrue(*(Where + TEXT(": CONTROL: the run record was written earlier in the respec's frame")), bWrittenFirst);
		TestTrue(*(Where + TEXT(": the respec is done")), bRespecced);
		TestEqual(*(Where + TEXT(": all 26 points are returned")), Returned, 26);
		TestTrue(*(Where + TEXT(": into the bank, 0.5 to 26.5")), Account->EmpireUpgradePoints == 26.5);
		TestEqual(*(Where + TEXT(": the tree holds nothing")), Account->EmpireTree.Nodes.Num(), 0);
		TestEqual(*(Where + TEXT(": the choice is forgotten too")), Account->EmpireTree.ChosenOptionIn(Foundations), 0);
		TestEqual(*(Where + TEXT(": it costs the mode's days")), Days, Mode.Days);
		TestEqual(*(Where + TEXT(": which are the run's death cost")), Days, Run->DeathDayCost());
		TestEqual(*(Where + TEXT(": and the run passed them")), Run->Day(), DayBefore + Mode.Days);
		TestEqual(*(Where + TEXT(": two writes started, the account record's and the run record's")), Writes, 2);
		if (TestNotNull(*(Where + TEXT(": the account record reads back from its slot")), Read))
		{
			TestEqual(*(Where + TEXT(": holding the empty tree")), Read->EmpireTree.Nodes.Num(), 0);
			TestTrue(*(Where + TEXT(": and the full bank")), Read->EmpireUpgradePoints == 26.5);
		}
		if (TestNotNull(*(Where + TEXT(": the run record reads back from its slot")), RunRead))
		{
			TestEqual(*(Where + TEXT(": holding the day the respec reached, not the day before it")), RunRead->Day,
					  Run->Day());
		}
	}
	return true;
}

/**
 * A respec is refused, changing nothing and passing no day, while a dungeon is being walked, when there is no run, and
 * when the tree holds nothing; and once the walk ends the same respec is done. Prints "REFUSED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAccountRespecRefusedTest,
	"Cataclysm.EmpireTree.ARespecIsRefusedInAWalkWithNoRunOrWithNothingSpent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAccountRespecRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeAccountTest;

	FTreeScene WithARun = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound*/ false, /*bWriting*/ false,
									/*bWithARun*/ true);
	FTreeScene NoRun = MakeScene(ECataclysmLethality::Hardcore, /*bSoloSelfFound*/ false, /*bWriting*/ false,
								 /*bWithARun*/ false);
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { WithARun.World, NoRun.World })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	UCataclysmEmpireRun* Run = WithARun.World ? UCataclysmGameInstance::EmpireRunFor(WithARun.World, false) : nullptr;
	UCataclysmAccountSave* Account = WithARun.Account();
	UCataclysmAccountSave* NoRunAccount = NoRun.Account();
	if (!TestTrue(TEXT("set-up: the two scenes were built"), WithARun.IsUsable() && NoRun.IsUsable())
		|| !TestTrue(TEXT("set-up: one has a run and the other none"),
					 Run != nullptr && UCataclysmGameInstance::EmpireRunFor(NoRun.World, false) == nullptr)
		|| !TestTrue(TEXT("set-up: and each its account record"), Account != nullptr && NoRunAccount != nullptr))
	{
		return false;
	}

	// NOTHING SPENT: refused before anything else is asked.
	int32 Returned = 0;
	int32 Days = 0;
	FString Empty;
	const bool bEmpty = UCataclysmGameInstance::RespecEmpireTree(WithARun.World, Returned, Days, Empty);

	// IN A WALK: the clock says a dungeon is being stood in, as entering one does.
	Account->EmpireTree.Add(StructuralIntegrity, 2);
	Account->EmpireUpgradePoints = 1.0;
	NoRunAccount->EmpireTree.Add(StructuralIntegrity, 2);
	NoRunAccount->EmpireUpgradePoints = 1.0;
	const int32 DayBefore = Run->Day();
	Run->Clock->EnterDungeon(/*DungeonId*/ 7);
	FString InAWalk;
	const bool bInAWalk = UCataclysmGameInstance::RespecEmpireTree(WithARun.World, Returned, Days, InAWalk);
	const int32 DayInTheWalk = Run->Day();
	const int32 PointsInTheWalk = Account->EmpireTree.Total();
	const double BankInTheWalk = Account->EmpireUpgradePoints;

	// NO RUN.
	FString Runless;
	const bool bRunless = UCataclysmGameInstance::RespecEmpireTree(NoRun.World, Returned, Days, Runless);

	// AND ONCE THE WALK HAS ENDED, the same respec is done: the refusal was the walk's.
	Run->Clock->LeaveDungeon();
	FString After;
	const bool bAfter = UCataclysmGameInstance::RespecEmpireTree(WithARun.World, Returned, Days, After);

	AddInfo(FString::Printf(TEXT("REFUSED: nothing spent %s, \"%s\"; in a walk %s, \"%s\", day %d to %d, %d point(s) "
								 "and %g banked kept; no run %s, \"%s\", %d point(s) kept; after the walk %s, %d point(s) "
								 "returned, %d day(s)"),
							bEmpty ? TEXT("done") : TEXT("refused"), *Empty, bInAWalk ? TEXT("done") : TEXT("refused"),
							*InAWalk, DayBefore, DayInTheWalk, PointsInTheWalk, BankInTheWalk,
							bRunless ? TEXT("done") : TEXT("refused"), *Runless, NoRunAccount->EmpireTree.Total(),
							bAfter ? TEXT("done") : TEXT("refused"), Returned, Days));

	TestFalse(TEXT("a tree with nothing spent is not respecced"), bEmpty);
	TestTrue(TEXT("and says there is nothing to respec"), Empty.Contains(TEXT("nothing to respec")));
	TestFalse(TEXT("a respec in a walk is refused"), bInAWalk);
	// THE ONE SENTENCE BUYING, CHOOSING AND RESPEC SHARE SINCE SLICE 2 (`UCataclysmGameInstance::EmpireTreeChangeRefusal`).
	TestTrue(TEXT("and says it is changed in the capital, not during a walk"),
			 InAWalk.Contains(TEXT("in the capital, not during a walk")));
	TestEqual(TEXT("passing no day"), DayInTheWalk, DayBefore);
	TestEqual(TEXT("and keeping the tree"), PointsInTheWalk, 2);
	TestTrue(TEXT("and the bank"), BankInTheWalk == 1.0);
	TestFalse(TEXT("a respec with no run is refused"), bRunless);
	TestTrue(TEXT("and says no run is being played"), Runless.Contains(TEXT("no run is being played")));
	TestEqual(TEXT("keeping that tree"), NoRunAccount->EmpireTree.Total(), 2);
	TestTrue(TEXT("CONTROL: once the walk has ended the same respec is done"), bAfter);
	TestEqual(TEXT("returning its two points"), Returned, 2);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
