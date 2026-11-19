// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A change the player makes to the character's build writes the character record. Issue #2763.
 *
 * WHY. The project owner spent passive points in a dungeon on 2026-11-19 and stopped Play In Editor 43 seconds after
 * entering it. Their character record, last written when they pressed Walk, held no passive node and no attribute
 * point: nothing wrote the record when a point was spent, and the clock writes the run record only.
 * `ECataclysmSaveTrigger::CharacterBuildChanged` now does, raised by the player's acts.
 *
 * EACH TEST WRITES THE RUN RECORD FIRST IN THE SAME FRAME, through the clock, as the clock's interval write could, and
 * then reads the change back from the character record's slot as the next session would. The run record's write that
 * the trigger asks for is skipped as written this frame, so the one write a change starts is the character record's.
 * The last test is the control: putting a build back, as a load and a test do, starts no write at all.
 *
 * Registered under `Cataclysm.BuildSave.`, which no other group's name begins with. The character record's slot is
 * named for an identifier each scene makes new, and `FCataclysmTestSaves` deletes it when the test ends.
 */
namespace CataclysmBuildSaveTest
{
	const FName Fervour(TEXT("Masochist_basic_spine_000"));	  // the Masochist tree's root, 1 point
	const FName FirstVow(TEXT("Masochist_capstone_25"));	  // opens at 25 points; option 2 is Reprisal Wave
	const FName Pyroclasm(TEXT("Demonic_Greataxe_Ultimate")); // in the starting Greataxe's Demonic pool
	const TCHAR* const Ferocity = TEXT("Ferocity");

	/** A possessed player, its state and the world's save writer. */
	struct FBuildSaveScene
	{
		UWorld* World = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmSaveWriter* Writer = nullptr;

		bool IsUsable() const { return World && State && Character && Writer; }
	};

	/**
	 * A world that has begun play, this project's game instance, and a possessed player of the Standard offline
	 * partition at `Level` with no experience: the Greataxe it begins with carries Demonic, which reaches the Masochist
	 * tree and gives the pool Pyroclasm is in. The save writer begins a run, as play does, so the triggers write.
	 */
	FBuildSaveScene MakeBuildSaveScene(int32 Level)
	{
		FBuildSaveScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.World->SetGameInstance(NewObject<UCataclysmGameInstance>());

		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Out.State || !Controller || !Out.Character)
		{
			return Out;
		}
		Controller->SetPlayerState(Out.State);
		Controller->Possess(Out.Character);

		FCataclysmCharacterPartition Partition;
		Partition.Lethality = ECataclysmLethality::Standard;
		Partition.Population = ECataclysmPopulation::Offline;
		Partition.bSoloSelfFound = false;
		Out.State->SetIdentity(TEXT("Builder"), Partition);
		Out.State->SetLevelAndExperience(Level, 0);

		Out.Writer = UCataclysmSaveWriter::In(Out.World);
		if (Out.Writer)
		{
			Out.Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
		}
		return Out;
	}

	/** The scene, checked: usable, writing, reaching the Masochist tree. One set-up check. */
	bool SceneIsReady(FAutomationTestBase& Test, const FBuildSaveScene& Scene)
	{
		return Test.TestTrue(TEXT("set-up: a possessed player, its state and a save writer that is writing, reaching "
								  "the Masochist tree"),
							 Scene.IsUsable() && Scene.Writer->IsWriting()
								 && Scene.State->ReachableTrees().Contains(TEXT("Masochist")));
	}

	/** Waits for any write to the slot to finish, giving up after two seconds. */
	void WaitForTheSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}

	/** The character record in the writer's slot, read as the next session would read it, or null. */
	const UCataclysmCharacterSave* ReadTheCharacterSlot(const UCataclysmSaveWriter& Writer)
	{
		const FString Slot = Writer.CharacterSlotName();
		WaitForTheSlot(Slot);
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString Message;
		return Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::ReadFromSlot(
			Slot, UCataclysmSaveWriter::UserIndex, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result,
			Message));
	}

	/** `Points` points across the Masochist tree's basic nodes, each to its maximum, in table order. */
	bool FillMasochistBasicNodes(int32 Points, FCataclysmPassiveAllocation& Out)
	{
		const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
		if (!NodeTable)
		{
			return false;
		}
		int32 Placed = 0;
		for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
		{
			const FCataclysmPassiveNodeRow* Row = reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value);
			if (Placed >= Points || !Row || Row->Tree != TEXT("Masochist") || Row->Kind != TEXT("basic")
				|| Row->MaxPoints <= 0)
			{
				continue;
			}
			const int32 Take = FMath::Min(Row->MaxPoints, Points - Placed);
			Out.Add(Pair.Key, Take);
			Placed += Take;
		}
		return Placed == Points;
	}

	/** The row on `Slot` in a saved assignment, or None. */
	FName SavedOn(const TArray<FCataclysmSkillChoice>& Choices, ECataclysmAbilitySlot Slot)
	{
		const FCataclysmSkillChoice* Found =
			Choices.FindByPredicate([Slot](const FCataclysmSkillChoice& Choice) { return Choice.Slot == Slot; });
		return Found ? Found->SkillRow : NAME_None;
	}
}

// `using namespace` inside each test, never at file scope: a unity build concatenates this file with others.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuildSavePassivePointTest,
	"Cataclysm.BuildSave.APassivePointSpentIsWrittenToTheCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuildSavePassivePointTest::RunTest(const FString&)
{
	using namespace CataclysmBuildSaveTest;

	FBuildSaveScene Scene = MakeBuildSaveScene(/*Level*/ 5);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!SceneIsReady(*this, Scene))
	{
		return false;
	}

	const bool bWrittenFirst = Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	const int32 Before = Scene.Writer->WritesStarted();
	FString Reason;
	const bool bSpent = Scene.State->SpendPassivePoint(Fervour, Reason);
	const int32 Writes = Scene.Writer->WritesStarted() - Before;
	const UCataclysmCharacterSave* Read = ReadTheCharacterSlot(*Scene.Writer);

	AddInfo(FString::Printf(TEXT("BUILD SAVE PASSIVE: the run written first %s; %s (%s) with %d write(s); the slot %s, "
								 "holding %d in Fervour and %d spent"),
							bWrittenFirst ? TEXT("yes") : TEXT("NO"), bSpent ? TEXT("spent") : TEXT("refused"),
							*Reason, Writes, Read ? TEXT("read") : TEXT("did not read"),
							Read ? Read->PassiveAllocation.PointsIn(Fervour) : -1,
							Read ? Read->PassiveAllocation.Total() : -1));

	TestTrue(TEXT("CONTROL: the run record was written earlier in this frame"), bWrittenFirst);
	TestTrue(TEXT("Fervour takes the point"), bSpent);
	TestEqual(TEXT("ONE WRITE STARTED, THE CHARACTER RECORD'S: the run record's was skipped as written this frame"),
			  Writes, 1);
	if (!TestNotNull(TEXT("the character record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("THE SLOT HOLDS THE POINT, which stopping play straight after the spend would have lost"),
			  Read->PassiveAllocation.PointsIn(Fervour), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuildSaveCapstoneTest,
	"Cataclysm.BuildSave.ACapstoneOptionTakenIsWrittenToTheCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuildSaveCapstoneTest::RunTest(const FString&)
{
	using namespace CataclysmBuildSaveTest;

	FBuildSaveScene Scene = MakeBuildSaveScene(/*Level*/ 30);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!SceneIsReady(*this, Scene))
	{
		return false;
	}

	// THE 25 POINTS THE FIRST VOW OPENS AT, HANDED OVER AS A LOAD HANDS THEM, which writes nothing.
	FCataclysmPassiveAllocation TwentyFive;
	if (!TestTrue(TEXT("set-up: the Masochist tree's basic nodes hold 25 points"),
				  FillMasochistBasicNodes(25, TwentyFive)))
	{
		return false;
	}
	Scene.State->SetPassiveAllocation(TwentyFive, TArray<FName>());

	const bool bWrittenFirst = Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	const int32 Before = Scene.Writer->WritesStarted();
	FString Reason;
	const bool bTaken = Scene.State->ChoosePassiveOption(FirstVow, 2, Reason);
	const int32 Writes = Scene.Writer->WritesStarted() - Before;
	const UCataclysmCharacterSave* Read = ReadTheCharacterSlot(*Scene.Writer);

	AddInfo(FString::Printf(TEXT("BUILD SAVE CAPSTONE: the run written first %s; %s (%s) with %d write(s); the slot %s, "
								 "holding option %d and %d point(s) at The First Vow, %d spent"),
							bWrittenFirst ? TEXT("yes") : TEXT("NO"), bTaken ? TEXT("taken") : TEXT("refused"),
							*Reason, Writes, Read ? TEXT("read") : TEXT("did not read"),
							Read ? Read->PassiveAllocation.ChosenOptionIn(FirstVow) : -1,
							Read ? Read->PassiveAllocation.PointsIn(FirstVow) : -1,
							Read ? Read->PassiveAllocation.Total() : -1));

	TestTrue(TEXT("CONTROL: the run record was written earlier in this frame"), bWrittenFirst);
	TestTrue(TEXT("The First Vow's option 2 is taken"), bTaken);
	TestEqual(TEXT("one write started, the character record's"), Writes, 1);
	if (!TestNotNull(TEXT("the character record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("THE SLOT HOLDS THE OPTION"), Read->PassiveAllocation.ChosenOptionIn(FirstVow), 2);
	TestEqual(TEXT("and the capstone's point"), Read->PassiveAllocation.PointsIn(FirstVow), 1);
	TestEqual(TEXT("with the 25 handed over: 26"), Read->PassiveAllocation.Total(), 26);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuildSaveAttributeTest,
	"Cataclysm.BuildSave.AnAttributePointSpentIsWrittenToTheCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuildSaveAttributeTest::RunTest(const FString&)
{
	using namespace CataclysmBuildSaveTest;

	FBuildSaveScene Scene = MakeBuildSaveScene(/*Level*/ 5);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!SceneIsReady(*this, Scene))
	{
		return false;
	}

	const bool bWrittenFirst = Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	const int32 Before = Scene.Writer->WritesStarted();
	FString Reason;
	const bool bSpent = Scene.State->SpendAttributePoints(Ferocity, 2, Reason);
	const int32 Writes = Scene.Writer->WritesStarted() - Before;
	const UCataclysmCharacterSave* Read = ReadTheCharacterSlot(*Scene.Writer);

	AddInfo(FString::Printf(TEXT("BUILD SAVE ATTRIBUTE: the run written first %s; %s (%s) with %d write(s); the slot "
								 "%s, holding %d in Ferocity"),
							bWrittenFirst ? TEXT("yes") : TEXT("NO"), bSpent ? TEXT("spent") : TEXT("refused"),
							*Reason, Writes, Read ? TEXT("read") : TEXT("did not read"),
							Read ? Read->SpentAttributePoints.PointsIn(Ferocity) : -1));

	TestTrue(TEXT("CONTROL: the run record was written earlier in this frame"), bWrittenFirst);
	TestTrue(TEXT("two points go into Ferocity"), bSpent);
	TestEqual(TEXT("one write started, the character record's"), Writes, 1);
	if (!TestNotNull(TEXT("the character record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("THE SLOT HOLDS BOTH POINTS"), Read->SpentAttributePoints.PointsIn(Ferocity), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuildSaveResetTest,
	"Cataclysm.BuildSave.AResetOfEitherIsWrittenToTheCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuildSaveResetTest::RunTest(const FString&)
{
	using namespace CataclysmBuildSaveTest;

	FBuildSaveScene Scene = MakeBuildSaveScene(/*Level*/ 5);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!SceneIsReady(*this, Scene))
	{
		return false;
	}

	const bool bWrittenFirst = Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	FString Reason;
	if (!TestTrue(TEXT("set-up: Fervour takes a point"), Scene.State->SpendPassivePoint(Fervour, Reason))
		|| !TestTrue(TEXT("set-up: and Ferocity two"), Scene.State->SpendAttributePoints(Ferocity, 2, Reason)))
	{
		AddError(Reason);
		return false;
	}

	const int32 Before = Scene.Writer->WritesStarted();
	Scene.State->ResetPassivePoints();
	Scene.State->ResetAttributePoints();
	const int32 Writes = Scene.Writer->WritesStarted() - Before;
	const UCataclysmCharacterSave* Read = ReadTheCharacterSlot(*Scene.Writer);

	AddInfo(FString::Printf(TEXT("BUILD SAVE RESET: the run written first %s; both reset with %d write(s); the slot %s, "
								 "holding %d passive and %d attribute points"),
							bWrittenFirst ? TEXT("yes") : TEXT("NO"), Writes, Read ? TEXT("read") : TEXT("did not read"),
							Read ? Read->PassiveAllocation.Total() : -1,
							Read ? Read->SpentAttributePoints.Total() : -1));

	TestTrue(TEXT("CONTROL: the run record was written earlier in this frame"), bWrittenFirst);
	TestEqual(TEXT("two writes started, one for each reset, both the character record's"), Writes, 2);
	if (!TestNotNull(TEXT("the character record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("THE SLOT HOLDS NO PASSIVE POINT"), Read->PassiveAllocation.Total(), 0);
	TestEqual(TEXT("AND NO ATTRIBUTE POINT"), Read->SpentAttributePoints.Total(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuildSaveSkillTest,
	"Cataclysm.BuildSave.ASkillChosenAndAChoiceForgottenAreWrittenToTheCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuildSaveSkillTest::RunTest(const FString&)
{
	using namespace CataclysmBuildSaveTest;

	FBuildSaveScene Scene = MakeBuildSaveScene(/*Level*/ 5);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!SceneIsReady(*this, Scene))
	{
		return false;
	}

	const bool bWrittenFirst = Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	int32 Before = Scene.Writer->WritesStarted();
	FString Reason;
	const bool bChosen = Scene.State->ChooseSkill(ECataclysmAbilitySlot::Heavy, Pyroclasm, Reason);
	const int32 ChooseWrites = Scene.Writer->WritesStarted() - Before;
	const UCataclysmCharacterSave* Chosen = ReadTheCharacterSlot(*Scene.Writer);
	const FName SavedAfterChoosing = Chosen ? SavedOn(Chosen->SkillAssignment, ECataclysmAbilitySlot::Heavy) : NAME_None;

	Before = Scene.Writer->WritesStarted();
	const bool bForgot = Scene.State->ForgetSkillChoice(ECataclysmAbilitySlot::Heavy);
	const int32 ForgetWrites = Scene.Writer->WritesStarted() - Before;
	const UCataclysmCharacterSave* Forgotten = ReadTheCharacterSlot(*Scene.Writer);

	AddInfo(FString::Printf(TEXT("BUILD SAVE SKILL: the run written first %s; %s (%s) with %d write(s), the slot holding "
								 "'%s' on Heavy; %s with %d write(s), the slot holding %d choice(s)"),
							bWrittenFirst ? TEXT("yes") : TEXT("NO"), bChosen ? TEXT("chosen") : TEXT("refused"),
							*Reason, ChooseWrites, *SavedAfterChoosing.ToString(),
							bForgot ? TEXT("forgotten") : TEXT("nothing to forget"), ForgetWrites,
							Forgotten ? Forgotten->SkillAssignment.Num() : -1));

	TestTrue(TEXT("CONTROL: the run record was written earlier in this frame"), bWrittenFirst);
	TestTrue(TEXT("Pyroclasm goes on the Heavy key"), bChosen);
	TestEqual(TEXT("one write started for the choice, the character record's"), ChooseWrites, 1);
	TestEqual(TEXT("THE SLOT HOLDS THE CHOICE"), SavedAfterChoosing, Pyroclasm);
	TestTrue(TEXT("the choice is forgotten"), bForgot);
	TestEqual(TEXT("one write started for forgetting it"), ForgetWrites, 1);
	if (!TestNotNull(TEXT("the character record reads back after the choice is forgotten"), Forgotten))
	{
		return false;
	}
	TestEqual(TEXT("AND THE SLOT HOLDS NO CHOICE"), Forgotten->SkillAssignment.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuildSaveRestoreTest,
	"Cataclysm.BuildSave.PuttingABuildBackWritesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuildSaveRestoreTest::RunTest(const FString&)
{
	using namespace CataclysmBuildSaveTest;

	FBuildSaveScene Scene = MakeBuildSaveScene(/*Level*/ 30);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!SceneIsReady(*this, Scene))
	{
		return false;
	}

	FCataclysmPassiveAllocation TwentyFive;
	FCataclysmAttributePoints TwoInFerocity;
	if (!TestTrue(TEXT("set-up: the Masochist tree's basic nodes hold 25 points"),
				  FillMasochistBasicNodes(25, TwentyFive))
		|| !TestTrue(TEXT("set-up: two points in Ferocity"), TwoInFerocity.AddTo(Ferocity, 2)))
	{
		return false;
	}

	// WHAT A LOAD DOES, AND WHAT A TEST DOES: no write. `FCataclysmSaveApply` puts a record back through the restores
	// and `AssignSkill`; tests hand allocations over through `SetPassiveAllocation`.
	const int32 Before = Scene.Writer->WritesStarted();
	FString Reason;
	Scene.State->RestorePassiveAllocation(TwentyFive, TArray<FName>(), 0);
	Scene.State->RestoreSpentAttributePoints(TwoInFerocity);
	Scene.State->SetPassiveAllocation(TwentyFive, TArray<FName>());
	const bool bAssigned = Scene.State->AssignSkill(ECataclysmAbilitySlot::Heavy, Pyroclasm, Reason);
	const bool bCleared = Scene.State->ClearSkillChoice(ECataclysmAbilitySlot::Heavy);
	const int32 PutBackWrites = Scene.Writer->WritesStarted() - Before;

	// THE CONTROL: this scene does write, and the player's act is what writes. Nothing wrote the run record this
	// frame, so the act writes it as well as the character record.
	const int32 BeforeTheAct = Scene.Writer->WritesStarted();
	const bool bChosen = Scene.State->ChooseSkill(ECataclysmAbilitySlot::Heavy, Pyroclasm, Reason);
	const int32 ActWrites = Scene.Writer->WritesStarted() - BeforeTheAct;

	AddInfo(FString::Printf(TEXT("BUILD SAVE PUT BACK: the restores, the handed allocation, an assignment (%s) and a "
								 "clear (%s) started %d write(s); then the player's choice (%s) started %d"),
							bAssigned ? TEXT("recorded") : TEXT("refused"), bCleared ? TEXT("forgotten") : TEXT("none"),
							PutBackWrites, bChosen ? TEXT("chosen") : TEXT("refused"), ActWrites));

	TestTrue(TEXT("set-up: AssignSkill records the choice, as a load's does"), bAssigned);
	TestTrue(TEXT("set-up: and ClearSkillChoice forgets it"), bCleared);
	TestEqual(TEXT("PUTTING A BUILD BACK STARTS NO WRITE"), PutBackWrites, 0);
	TestTrue(TEXT("CONTROL: the player's choice is made"), bChosen);
	TestEqual(TEXT("CONTROL: and it starts two writes, the run record's and the character record's"), ActWrites, 2);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
