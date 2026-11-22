// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A Solo Self-Found character's own empire upgrade points and tree, on its character record. Issue #2741, slice 1c:
 * the owner reversed the coordinating session's C8 on 2026-11-18 ("Include them"), so such a character banks a
 * defeat's award and spends it in a tree of its own, which shares nothing with any other character.
 *
 * WHAT IS CHECKED HERE: the character record's version 9 step, the gather and the restore of the character's own bank
 * and tree, and that buying and a respec write its own record and no account record. Which tree `EmpireTreeHeldBy`
 * hands out is checked in `CataclysmEmpireTreeAccountTests.cpp`, and a defeat banking into the character's own in
 * `Cataclysm.EmpirePoints.ASoloSelfFoundDefeatBanksNothingInAnyAccount`.
 *
 * A SOLO SELF-FOUND CHARACTER HAS NO ACCOUNT SLOT (`UCataclysmGameInstance::AccountSlotFor` answers empty), so nothing
 * here can touch the player's own account; its character record's slot is named for an identifier each scene makes
 * new, and `FCataclysmTestSaves` deletes it when the test ends.
 */
namespace CataclysmEmpireTreeOwnTest
{
	const FName StructuralIntegrity(TEXT("basic_0000000w"));	// Architect, a root, 10 points
	const FName FortifiedGates(TEXT("basic_arch_t1d"));			// <- Structural Integrity at 2, 8 points

	/** A world with this project's game instance and a possessed player of one partition. */
	struct FOwnScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State && Character; }
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

	/**
	 * The scene: a partition of the lethality mode given, offline. `bWriting` begins the save writer, as play does, so
	 * the triggers write. `bWithARun` begins a run at the lethality mode's rung, as a character's run is begun.
	 */
	FOwnScene MakeScene(ECataclysmLethality Lethality, bool bSoloSelfFound, bool bWriting, bool bWithARun)
	{
		FOwnScene Out;
		Out.Partition.Lethality = Lethality;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

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
			Out.State->SetIdentity(TEXT("Hermit"), Out.Partition);
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

	/** The character record in a slot, read as the next session would read it, or null. */
	const UCataclysmCharacterSave* ReadTheCharacterSlot(const FString& Slot)
	{
		WaitForTheSlot(Slot);
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString Message;
		return Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::ReadFromSlot(
			Slot, UCataclysmSaveWriter::UserIndex, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result,
			Message));
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
}

// NO `using namespace` AT FILE SCOPE, for the reason `CataclysmEmpireTreeTests.cpp` gives: in a unity build it would
// reach the files compiled after this one. Each test opens the namespace inside its body.

/**
 * `Migrate_8_to_9` keeps a character's private points and writes it an empty tree of its own, keeps a tree already
 * there, and refuses points that are not a number or a tree that is not an object: run on the committed version 8 file
 * as it is, with 7 points and a tree of one node, and with a word in each place. And `Character_v8.json` reads through
 * the chain into a record at the current version with every other field kept. Prints "CHARACTER MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeOwnMigrationTest,
	"Cataclysm.EmpireTree.ACharacterFromBeforeItsOwnTreeMigratesKeepingItsPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeOwnMigrationTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v8.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	// FOUR COPIES AT VERSION 8: as the file is; with 7 points and a tree; with a word for the points; and for the tree.
	const TSharedPtr<FJsonObject> AsItIs = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> Held = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> WordPoints = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> WordTree = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Character_v8.json is a JSON object"),
				  AsItIs.IsValid() && Held.IsValid() && WordPoints.IsValid() && WordTree.IsValid())
		|| !TestFalse(TEXT("set-up: and holds no tree of its own"), AsItIs->HasField(TEXT("PrivateEmpireTree"))))
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
	Held->SetNumberField(TEXT("PrivateEmpireUpgradePoints"), 7);
	Held->SetObjectField(TEXT("PrivateEmpireTree"), Tree);
	WordPoints->SetStringField(TEXT("PrivateEmpireUpgradePoints"), TEXT("plenty"));
	WordTree->SetStringField(TEXT("PrivateEmpireTree"), TEXT("everything"));

	const TArrayView<const FCataclysmSaveMigrationStep> Steps = GetDefault<UCataclysmCharacterSave>()->MigrationSteps();
	FString AsItIsMessage;
	FString HeldMessage;
	FString WordPointsMessage;
	FString WordTreeMessage;
	const ECataclysmSaveMigrationResult AsItIsResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmCharacterSave::SchemaVersionNow, AsItIs.ToSharedRef(), AsItIsMessage);
	const ECataclysmSaveMigrationResult HeldResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmCharacterSave::SchemaVersionNow, Held.ToSharedRef(), HeldMessage);
	const ECataclysmSaveMigrationResult WordPointsResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmCharacterSave::SchemaVersionNow, WordPoints.ToSharedRef(), WordPointsMessage);
	const ECataclysmSaveMigrationResult WordTreeResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmCharacterSave::SchemaVersionNow, WordTree.ToSharedRef(), WordTreeMessage);

	double AsItIsPoints = -1.0;
	double HeldPoints = -1.0;
	AsItIs->TryGetNumberField(TEXT("PrivateEmpireUpgradePoints"), AsItIsPoints);
	Held->TryGetNumberField(TEXT("PrivateEmpireUpgradePoints"), HeldPoints);
	const TSharedPtr<FJsonObject>* Written = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* WrittenNodes = nullptr;
	const bool bWritten = AsItIs->TryGetObjectField(TEXT("PrivateEmpireTree"), Written) && Written != nullptr
		&& Written->IsValid() && (*Written)->TryGetArrayField(TEXT("Nodes"), WrittenNodes) && WrittenNodes != nullptr;
	const TSharedPtr<FJsonObject>* Kept = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* KeptNodes = nullptr;
	const bool bKept = Held->TryGetObjectField(TEXT("PrivateEmpireTree"), Kept) && Kept != nullptr && Kept->IsValid()
		&& (*Kept)->TryGetArrayField(TEXT("Nodes"), KeptNodes) && KeptNodes != nullptr;

	AddInfo(FString::Printf(TEXT("CHARACTER MIGRATED: as it is %d (%s) -> %g points, %s; 7 and a tree %d (%s) -> %g, %d "
								 "node(s); a word for the points %d (%s); a word for the tree %d (%s)"),
							static_cast<int32>(AsItIsResult), *AsItIsMessage, AsItIsPoints,
							bWritten ? *FString::Printf(TEXT("a tree of %d node(s)"), WrittenNodes->Num())
									 : TEXT("NO TREE"),
							static_cast<int32>(HeldResult), *HeldMessage, HeldPoints, bKept ? KeptNodes->Num() : -1,
							static_cast<int32>(WordPointsResult), *WordPointsMessage,
							static_cast<int32>(WordTreeResult), *WordTreeMessage));

	TestEqual(TEXT("a character at 8 migrates"), static_cast<int32>(AsItIsResult),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("keeping its private points, 0"), AsItIsPoints, 0.0);
	TestTrue(TEXT("and gaining an empty tree of its own, written out"), bWritten && WrittenNodes->Num() == 0);
	TestEqual(TEXT("one holding 7 points keeps them"), HeldPoints, 7.0);
	TestTrue(TEXT("and its tree of one node"), bKept && KeptNodes->Num() == 1);
	TestNotEqual(TEXT("one holding a word for its points is refused"), static_cast<int32>(WordPointsResult),
				 static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestNotEqual(TEXT("and one holding a word for its tree"), static_cast<int32>(WordTreeResult),
				 static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));

	// THE FILE FROM BEFORE THE TREE, through the chain, compared with what the record writes back.
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v8.json would not load: %s -- %s"),
								 FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmCharacterSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheCharactersVersion9Defaults(*Expected);
	CataclysmSaveFixtures::AddTheCharactersVersion10Defaults(*Expected); // And 10, issue #2764: no skill experience.
	CataclysmSaveFixtures::AddTheCharactersVersion11Defaults(*Expected); // And 11, #2741 slice 6e: no debt.
	CataclysmSaveFixtures::AddTheCharactersVersion12Defaults(*Expected); // And 12, #2741 slice 8: no extra option.
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);
	AddInfo(FString::Printf(TEXT("CHARACTER MIGRATED: Character_v8.json %s, at version %d, %g private points, a tree of "
								 "%d node(s); the rest %s"),
							FCataclysmSaveStorage::Describe(Result), Read->SchemaVersion,
							Read->PrivateEmpireUpgradePoints, Read->PrivateEmpireTree.Nodes.Num(),
							bSame ? TEXT("kept") : *Where));
	TestEqual(TEXT("Character_v8.json is migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("to the current version"), Read->SchemaVersion, UCataclysmCharacterSave::SchemaVersionNow);
	TestEqual(TEXT("holding an empty tree of its own"), Read->PrivateEmpireTree.Nodes.Num(), 0);
	TestTrue(TEXT("and every other field"), bSame);
	return true;
}

/**
 * A Solo Self-Found character's own bank and tree survive its character record: the gather writes them, the JSON
 * round trip keeps them, and the restore puts them back on the player state. An ordinary character's record holds 0
 * and an empty tree, its tree being its lethality mode's. Prints "OWN TREE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeOwnRecordTest,
	"Cataclysm.EmpireTree.ASoloSelfFoundCharactersTreeSurvivesItsCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeOwnRecordTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeOwnTest;

	FOwnScene Solo = MakeScene(ECataclysmLethality::Hardcore, /*bSoloSelfFound*/ true, /*bWriting*/ false,
							   /*bWithARun*/ false);
	FOwnScene Ordinary = MakeScene(ECataclysmLethality::Hardcore, /*bSoloSelfFound*/ false, /*bWriting*/ false,
								   /*bWithARun*/ false);
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Solo.World, Ordinary.World })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the two scenes were built"), Solo.IsUsable() && Ordinary.IsUsable()))
	{
		return false;
	}

	FCataclysmPassiveAllocation Tree;
	Tree.Add(StructuralIntegrity, 3);
	Solo.State->SetOwnEmpireTree(2.5, Tree);

	UCataclysmCharacterSave* SoloRecord = NewObject<UCataclysmCharacterSave>();
	UCataclysmCharacterSave* OrdinaryRecord = NewObject<UCataclysmCharacterSave>();
	const bool bGathered = FCataclysmSaveGather::CharacterFrom(*Solo.Character, *SoloRecord)
		&& FCataclysmSaveGather::CharacterFrom(*Ordinary.Character, *OrdinaryRecord);
	SoloRecord->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;

	FString Json;
	FString Error;
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const bool bWritten = FCataclysmSaveStorage::ToJson(SoloRecord, Json, Error);
	const UCataclysmCharacterSave* ReadBack = bWritten ? Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message)) : nullptr;

	Solo.State->SetOwnEmpireTree(0.0, FCataclysmPassiveAllocation());
	int32 SkillsRefused = 0;
	FString Why;
	const bool bRestored = ReadBack != nullptr
		&& FCataclysmSaveApply::CharacterInto(*ReadBack, *Solo.Character, SkillsRefused, Why);

	AddInfo(FString::Printf(TEXT("OWN TREE: gathered %s, the Solo Self-Found record %g and %s, the ordinary record %g "
								 "and %s; written %s, read back %g and %s (%s); restored %s (%s), the state %g and %s"),
							bGathered ? TEXT("yes") : TEXT("NO"), SoloRecord->PrivateEmpireUpgradePoints,
							*Describe(SoloRecord->PrivateEmpireTree), OrdinaryRecord->PrivateEmpireUpgradePoints,
							*Describe(OrdinaryRecord->PrivateEmpireTree), bWritten ? TEXT("yes") : *Error,
							ReadBack ? ReadBack->PrivateEmpireUpgradePoints : -1.0,
							ReadBack ? *Describe(ReadBack->PrivateEmpireTree) : TEXT("-"),
							FCataclysmSaveStorage::Describe(Result), bRestored ? TEXT("yes") : TEXT("NO"), *Why,
							Solo.State->GetOwnEmpireUpgradePoints(), *Describe(Solo.State->GetOwnEmpireTree())));

	TestTrue(TEXT("both records are gathered"), bGathered);
	TestTrue(TEXT("the Solo Self-Found record holds its own 2.5 points"), SoloRecord->PrivateEmpireUpgradePoints == 2.5);
	TestEqual(TEXT("and its own Structural Integrity at 3"), SoloRecord->PrivateEmpireTree.PointsIn(StructuralIntegrity),
			  3);
	TestTrue(TEXT("the ordinary record holds no points of its own"), OrdinaryRecord->PrivateEmpireUpgradePoints == 0.0);
	TestEqual(TEXT("and no tree of its own: its tree is its mode's"), OrdinaryRecord->PrivateEmpireTree.Nodes.Num(), 0);
	TestTrue(TEXT("the record is written out"), bWritten);
	TestTrue(TEXT("and reads back holding both"),
			 ReadBack != nullptr && ReadBack->PrivateEmpireUpgradePoints == 2.5
				 && ReadBack->PrivateEmpireTree.PointsIn(StructuralIntegrity) == 3);
	TestTrue(TEXT("the restore succeeds"), bRestored);
	TestTrue(TEXT("and puts the 2.5 points back on the player state"), Solo.State->GetOwnEmpireUpgradePoints() == 2.5);
	TestEqual(TEXT("and the tree"), Solo.State->GetOwnEmpireTree().PointsIn(StructuralIntegrity), 3);
	return true;
}

/**
 * A point a Solo Self-Found character buys comes off its own bank and into its own tree, and its own character record
 * is written holding both: the slot reads them back, and no account record is made or held. Prints "OWN BUY".
 *
 * THE RUN RECORD IS WRITTEN FIRST, IN THE SAME FRAME, BY THE CLOCK. The save writer writes the run record at most once
 * a frame, and `OwnEmpireTreeChanged` is not in `UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame`, so the
 * buy's run-record write is skipped: the coordinating session asked, on 2026-11-18, whether the character-record write
 * is skipped with it. `UCataclysmSaveWriter::NoteTrigger` asks for each record on its own and only the run record's
 * write checks the frame, so it is not; this shows it, with one write started -- the character record's -- and the
 * point on disk.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeOwnBuyTest,
	"Cataclysm.EmpireTree.ASoloSelfFoundPointBoughtIsWrittenToItsOwnRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeOwnBuyTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeOwnTest;

	FOwnScene Scene = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound*/ true, /*bWriting*/ true,
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

	Scene.State->SetOwnEmpireTree(1.5, FCataclysmPassiveAllocation());

	// THE RUN RECORD WRITTEN EARLIER IN THIS FRAME, before the buy, as the clock's interval write could be.
	const bool bWrittenFirst = Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	const int32 WritesBefore = Writer->WritesStarted();
	FString Reason;
	const bool bBought = UCataclysmGameInstance::BuyEmpireTreePoint(Scene.World, StructuralIntegrity, Reason);
	const int32 Writes = Writer->WritesStarted() - WritesBefore;
	const FString Slot = Writer->CharacterSlotName();
	const UCataclysmCharacterSave* Read = ReadTheCharacterSlot(Slot);

	AddInfo(FString::Printf(TEXT("OWN BUY: the run written first %s; %s (%s) with %d write(s); its own tree %s, its own "
								 "bank %g; an account held %s, its account slot '%s'; the character slot '%s' %s, holding "
								 "%g and %s"),
							bWrittenFirst ? TEXT("yes") : TEXT("NO"), bBought ? TEXT("bought") : TEXT("refused"),
							*Reason, Writes,
							*Describe(Scene.State->GetOwnEmpireTree()), Scene.State->GetOwnEmpireUpgradePoints(),
							Scene.Instance->HeldAccount != nullptr ? TEXT("yes") : TEXT("no"),
							*UCataclysmGameInstance::AccountSlotFor(Scene.Partition), *Slot,
							Read != nullptr ? TEXT("read") : TEXT("did not read"),
							Read != nullptr ? Read->PrivateEmpireUpgradePoints : -1.0,
							Read != nullptr ? *Describe(Read->PrivateEmpireTree) : TEXT("-")));

	TestTrue(TEXT("CONTROL: the run record was written earlier in the buy's frame"), bWrittenFirst);
	TestTrue(TEXT("Structural Integrity is bought"), bBought);
	TestEqual(TEXT("into its own tree"), Scene.State->GetOwnEmpireTree().PointsIn(StructuralIntegrity), 1);
	TestTrue(TEXT("paid from its own bank, 1.5 to 0.5"), Scene.State->GetOwnEmpireUpgradePoints() == 0.5);
	TestTrue(TEXT("CONTROL: a Solo Self-Found character has no account slot"),
			 UCataclysmGameInstance::AccountSlotFor(Scene.Partition).IsEmpty());
	TestTrue(TEXT("and no account record is held for it"), Scene.Instance->HeldAccount == nullptr);
	TestEqual(TEXT("one write started, the character record's: the run record's was skipped as written this frame"),
			  Writes, 1);
	if (!TestNotNull(TEXT("its character record reads back from its slot"), Read))
	{
		return false;
	}
	TestTrue(TEXT("holding the bank after the point"), Read->PrivateEmpireUpgradePoints == 0.5);
	TestEqual(TEXT("and the point"), Read->PrivateEmpireTree.PointsIn(StructuralIntegrity), 1);
	return true;
}

/**
 * A Solo Self-Found character's respec returns its own points to its own bank and takes the mode's days off the run,
 * and both its character record and the run record hold it -- the run record written first in the same frame by the
 * clock, as in slice 1b's respec test. Prints "OWN RESPEC".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeOwnRespecTest,
	"Cataclysm.EmpireTree.ASoloSelfFoundRespecIsWrittenToItsOwnRecordAndTheRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeOwnRespecTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeOwnTest;

	FOwnScene Scene = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound*/ true, /*bWriting*/ true,
								/*bWithARun*/ true);
	UCataclysmSaveWriter* Writer = Scene.World ? UCataclysmSaveWriter::In(Scene.World) : nullptr;
	UCataclysmEmpireRun* Run = Scene.World ? UCataclysmGameInstance::EmpireRunFor(Scene.World, false) : nullptr;
	if (!TestTrue(TEXT("set-up: the scene, its writer and its run"),
				  Scene.IsUsable() && Writer != nullptr && Writer->IsWriting() && Run != nullptr))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCataclysmPassiveAllocation Tree;
	Tree.Add(StructuralIntegrity, 10);
	Tree.Add(FortifiedGates, 3);
	Scene.State->SetOwnEmpireTree(0.25, Tree);
	const int32 DayBefore = Run->Day();

	// THE RUN RECORD WRITTEN EARLIER IN THIS FRAME, before the respec, as the clock's interval write could be.
	const bool bWrittenFirst = Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);

	int32 Returned = 0;
	int32 Days = 0;
	FString Reason;
	const bool bRespecced = UCataclysmGameInstance::RespecEmpireTree(Scene.World, Returned, Days, Reason);
	const UCataclysmCharacterSave* Read = ReadTheCharacterSlot(Writer->CharacterSlotName());
	WaitForTheSlot(Writer->RunSlotName());
	ECataclysmSaveLoadResult RunResult = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString RunMessage;
	const UCataclysmRunSave* RunRead = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::ReadFromSlot(
		Writer->RunSlotName(), UCataclysmSaveWriter::UserIndex, UCataclysmRunSave::StaticClass(), GetTransientPackage(),
		RunResult, RunMessage));

	AddInfo(FString::Printf(TEXT("OWN RESPEC: the run written first %s; %s (%s), %d point(s) returned, %d day(s), day %d "
								 "to %d; its own bank %g and tree %s; the character slot %g and %s; the run slot's day %d; "
								 "an account held %s"),
							bWrittenFirst ? TEXT("yes") : TEXT("NO"), bRespecced ? TEXT("done") : TEXT("refused"),
							*Reason, Returned, Days, DayBefore, Run->Day(), Scene.State->GetOwnEmpireUpgradePoints(),
							*Describe(Scene.State->GetOwnEmpireTree()),
							Read != nullptr ? Read->PrivateEmpireUpgradePoints : -1.0,
							Read != nullptr ? *Describe(Read->PrivateEmpireTree) : TEXT("-"),
							RunRead != nullptr ? RunRead->Day : -1,
							Scene.Instance->HeldAccount != nullptr ? TEXT("yes") : TEXT("no")));

	TestTrue(TEXT("CONTROL: the run record was written earlier in the respec's frame"), bWrittenFirst);
	TestTrue(TEXT("the respec is done"), bRespecced);
	TestEqual(TEXT("its 13 points are returned"), Returned, 13);
	TestTrue(TEXT("into its own bank, 0.25 to 13.25"), Scene.State->GetOwnEmpireUpgradePoints() == 13.25);
	TestEqual(TEXT("its own tree holds nothing"), Scene.State->GetOwnEmpireTree().Nodes.Num(), 0);
	TestEqual(TEXT("it costs Standard's 5 days"), Days, 5);
	TestEqual(TEXT("and the run passed them"), Run->Day(), DayBefore + 5);
	TestTrue(TEXT("and no account record is held for it"), Scene.Instance->HeldAccount == nullptr);
	if (TestNotNull(TEXT("its character record reads back from its slot"), Read))
	{
		TestTrue(TEXT("holding the full bank"), Read->PrivateEmpireUpgradePoints == 13.25);
		TestEqual(TEXT("and the empty tree"), Read->PrivateEmpireTree.Nodes.Num(), 0);
	}
	if (TestNotNull(TEXT("the run record reads back from its slot"), RunRead))
	{
		TestEqual(TEXT("holding the day the respec reached, not the day before it"), RunRead->Day, Run->Day());
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
