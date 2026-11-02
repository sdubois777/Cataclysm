// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * Tests for the Consumption mark, the first slice of issue #2688.
 *
 * `docs/Cataclysm_GDD_v2.md`, "What crossing it does": "The character is marked." A confirmed equip that takes the
 * Worn Residue across the tier's Consumption Threshold marks the character; the character record holds it (ruled
 * 2026-11-02); and only killing the double clears it, the owner's decision of 2026-11-02, so falling back below the
 * threshold does not. The double itself is the second slice.
 *
 * NO TEST HERE SETS A TIER. A world built by a test has no game mode, so every equip is measured against tier 1's
 * threshold, 1950, the tier `ACataclysmGameMode::DifficultyTierFor` answers without one.
 */
namespace CataclysmConsumptionMarkTest
{
	static constexpr int32 UserIndex = UCataclysmSaveWriter::UserIndex;

	/** A world with a game instance, a possessed player with its state, and, when asked for, a save writer. */
	struct FMarkScene
	{
		UWorld* World = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmSaveWriter* Writer = nullptr;

		bool IsUsable() const
		{
			return World && State && Character && Character->GetInventory() && Character->GetEquipment();
		}
	};

	FMarkScene MakeScene(bool bWriting)
	{
		FMarkScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.World->SetGameInstance(NewObject<UCataclysmGameInstance>());
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
		}
		if (bWriting)
		{
			Out.Writer = UCataclysmSaveWriter::In(Out.World);
			if (Out.Writer)
			{
				Out.Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
			}
		}
		return Out;
	}

	FCataclysmItem Of(const TCHAR* Base, float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.Residue = Residue;
		return Item;
	}

	void Wear(ACataclysmPlayerCharacter& Character, const FCataclysmItem& Item)
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Character.GetEquipment()->Equip(Item, First, Second, Went);
	}

	/** Waits for every write to a slot to finish, then deletes it; for the save tests' reason, a test leaves none. */
	void Delete(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
		}
	}

	/** The character record in a slot, once every write to it has finished; null when there is none. */
	UCataclysmCharacterSave* ReadCharacter(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString Message;
		return Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::ReadFromSlot(
			SlotName, UserIndex, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	}

	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* BootsBase = TEXT("Boots_Sabatons");
}

// ---------------------------------------------------------------------------
// A confirmed crossing marks
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionMarkIsSetByACrossing,
	"Cataclysm.ConsumptionMark.AConfirmedCrossingEquipMarksTheCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionMarkIsSetByACrossing::RunTest(const FString& Parameters)
{
	using namespace CataclysmConsumptionMarkTest;

	FMarkScene Scene = MakeScene(/*bWriting=*/false);
	if (!TestTrue(TEXT("set-up: a possessed player with its state"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A helm of 1000 worn and boots of 1000 carried: past tier 1's 1950 once both are on.
	UCataclysmEquipmentComponent* Equipment = Scene.Character->GetEquipment();
	const float StartingGear = Equipment->WornResidue();
	TestTrue(TEXT("set-up: the starting gear carries under 950"), StartingGear < 950.0f);
	Wear(*Scene.Character, Of(HelmBase, 1000.0f));
	const FCataclysmItem Boots = Of(BootsBase, 1000.0f);
	const int32 Slot = Scene.Character->GetInventory()->AddItem(Boots);

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Asked = UCataclysmWearing::WearFromCarried(
		Scene.Character->GetInventory(), Equipment, Slot, Went);
	TestTrue(TEXT("CONTROL: the unconfirmed equip is asked about"),
			 Asked == ECataclysmWearResult::WouldCrossConsumptionThreshold);
	TestFalse(TEXT("CONTROL: and does not mark"), Scene.State->IsMarkedByConsumption());

	const ECataclysmWearResult Yes = Scene.Character->WearPastTheThreshold(Slot, &Boots, Went);
	TestTrue(TEXT("the confirmed crossing is worn"), Yes == ECataclysmWearResult::Worn);
	TestTrue(TEXT("the confirmed crossing marks the character"), Scene.State->IsMarkedByConsumption());

	AddInfo(FString::Printf(TEXT("CONSUMPTION MARK: starting gear %.0f; asked '%s'; yes '%s'; marked %s"), StartingGear,
							*UCataclysmWearing::Explain(Asked), *UCataclysmWearing::Explain(Yes),
							Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no")));
	return true;
}

// ---------------------------------------------------------------------------
// Only a crossing marks, and only killing the double clears it
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionMarkOnlyACrossing,
	"Cataclysm.ConsumptionMark.OnlyACrossingMarksAndFallingBackBelowDoesNotClearIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionMarkOnlyACrossing::RunTest(const FString& Parameters)
{
	using namespace CataclysmConsumptionMarkTest;

	FMarkScene Scene = MakeScene(/*bWriting=*/false);
	if (!TestTrue(TEXT("set-up: a possessed player with its state"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	UCataclysmInventoryComponent* Bag = Scene.Character->GetInventory();
	UCataclysmEquipmentComponent* Equipment = Scene.Character->GetEquipment();
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;

	// A YES THAT STAYS BELOW: worn, and no mark.
	Wear(*Scene.Character, Of(HelmBase, 100.0f));
	const int32 LowBoots = Bag->AddItem(Of(BootsBase, 100.0f));
	Scene.Character->WearPastTheThreshold(LowBoots, nullptr, Went);
	TestFalse(TEXT("a confirmed equip that stays below does not mark"), Scene.State->IsMarkedByConsumption());

	// A YES ABOUT ONE ITEM WHEN THE CELL HOLDS ANOTHER: nothing is worn, and the mark set for it is taken back.
	const FCataclysmItem HeavyHelm = Of(HelmBase, 2000.0f);
	const int32 HeavySlot = Bag->AddItem(HeavyHelm);
	const FCataclysmItem NotThere = Of(HelmBase, 2001.0f);
	const ECataclysmWearResult Moved = Scene.Character->WearPastTheThreshold(HeavySlot, &NotThere, Went);
	TestTrue(TEXT("set-up: a yes about an item no longer there is refused"), Moved == ECataclysmWearResult::NoLongerThere);
	TestFalse(TEXT("an equip that did not happen leaves the character unmarked"), Scene.State->IsMarkedByConsumption());

	// A CROSSING MARKS; FALLING BACK BELOW DOES NOT CLEAR IT (the owner, 2026-11-02).
	Scene.Character->WearPastTheThreshold(HeavySlot, &HeavyHelm, Went);
	TestTrue(TEXT("set-up: the crossing marks"), Scene.State->IsMarkedByConsumption());
	const int32 LightSlot = Bag->AddItem(Of(HelmBase, 10.0f));
	UCataclysmWearing::WearFromCarried(Bag, Equipment, LightSlot, Went);
	TestTrue(TEXT("set-up: the Worn Residue is below the threshold again"),
			 Equipment->WornResidue() < UCataclysmEquipmentComponent::ConsumptionThresholdFor(1));
	TestTrue(TEXT("falling back below the threshold leaves the mark"), Scene.State->IsMarkedByConsumption());
	return true;
}

// ---------------------------------------------------------------------------
// The record the equip writes already holds the mark
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionMarkWrittenByTheEquip,
	"Cataclysm.ConsumptionMark.TheEquipThatMarksWritesTheMarkIntoTheCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionMarkWrittenByTheEquip::RunTest(const FString& Parameters)
{
	using namespace CataclysmConsumptionMarkTest;

	FMarkScene Scene = MakeScene(/*bWriting=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player with its state and a writer"), Scene.IsUsable() && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { Delete(RunSlot); Delete(CharacterSlot); };

	Wear(*Scene.Character, Of(HelmBase, 1000.0f));
	const FCataclysmItem Boots = Of(BootsBase, 1000.0f);
	const int32 Slot = Scene.Character->GetInventory()->AddItem(Boots);

	// NO OTHER SAVE BETWEEN THE EQUIP AND THE READ: the record read is the one the equip's own trigger wrote.
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Yes = Scene.Character->WearPastTheThreshold(Slot, &Boots, Went);
	const UCataclysmCharacterSave* Record = ReadCharacter(CharacterSlot);
	TestTrue(TEXT("set-up: the confirmed crossing is worn"), Yes == ECataclysmWearResult::Worn);
	TestTrue(TEXT("set-up: and the character is marked"), Scene.State->IsMarkedByConsumption());
	if (!TestNotNull(TEXT("set-up: the equip wrote the character record"), Record))
	{
		return false;
	}
	TestTrue(TEXT("the record the equip wrote holds the mark"), Record->bMarkedByConsumption);
	AddInfo(FString::Printf(TEXT("CONSUMPTION MARK WRITTEN: record marked %s, at version %d"),
							Record->bMarkedByConsumption ? TEXT("yes") : TEXT("no"), Record->SchemaVersion));
	return true;
}

// ---------------------------------------------------------------------------
// Saved and loaded
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionMarkSavedAndLoaded,
	"Cataclysm.ConsumptionMark.TheMarkIsWrittenToTheCharacterRecordAndLoadsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionMarkSavedAndLoaded::RunTest(const FString& Parameters)
{
	using namespace CataclysmConsumptionMarkTest;

	FMarkScene Scene = MakeScene(/*bWriting=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player with its state and a writer"), Scene.IsUsable() && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { Delete(RunSlot); Delete(CharacterSlot); };

	Scene.State->MarkByConsumption();
	Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
	UCataclysmCharacterSave* Record = ReadCharacter(CharacterSlot);
	if (!TestNotNull(TEXT("set-up: the character record was written"), Record))
	{
		return false;
	}
	TestTrue(TEXT("the character record holds the mark"), Record->bMarkedByConsumption);

	FMarkScene Fresh = MakeScene(/*bWriting=*/false);
	if (!TestTrue(TEXT("set-up: a second, unmarked player"), Fresh.IsUsable()))
	{
		if (Fresh.World)
		{
			Fresh.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Fresh.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	int32 Refused = 0;
	FString Why;
	FCataclysmSaveApply::CharacterInto(*Record, *Fresh.Character, Refused, Why);
	TestTrue(TEXT("and a fresh character loaded from it is marked"), Fresh.State->IsMarkedByConsumption());

	// AND THE OTHER WAY: an unmarked record loaded over a marked character leaves it unmarked.
	Record->bMarkedByConsumption = false;
	FCataclysmSaveApply::CharacterInto(*Record, *Fresh.Character, Refused, Why);
	TestFalse(TEXT("an unmarked record loads unmarked over a marked character"), Fresh.State->IsMarkedByConsumption());
	return true;
}

// ---------------------------------------------------------------------------
// A record from before the mark
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionMarkMigration,
	"Cataclysm.ConsumptionMark.ACharacterFileFromBeforeTheMarkMigratesUnmarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionMarkMigration::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v4.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	// THE STEP, ON THE FILE.
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Character_v4.json is a JSON object"), Stepped.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("set-up: the version 4 file has no mark key"), Stepped->HasField(TEXT("bMarkedByConsumption")));
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmCharacterSave>()->MigrationSteps(), UCataclysmCharacterSave::SchemaVersionNow,
		Stepped.ToSharedRef(), StepMessage);
	bool bSteppedMark = true;
	const bool bWritten = Stepped->TryGetBoolField(TEXT("bMarkedByConsumption"), bSteppedMark);

	// THE RECORD.
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v4.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	// AND NOTHING ELSE MOVED.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmCharacterSave::SchemaVersionNow);
	Expected->SetBoolField(TEXT("bMarkedByConsumption"), false);
	Expected->SetObjectField(TEXT("ConsumptionBuild"), CataclysmSaveFixtures::EmptyPlayerBuild()); // Since 2b: on to 6.
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("MARK MIGRATED: the step %d (%s), wrote the key %s as %s; the record at version %d, "
								 "marked %s; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWritten ? TEXT("yes") : TEXT("NO"),
							bSteppedMark ? TEXT("true") : TEXT("false"), Read->SchemaVersion,
							Read->bMarkedByConsumption ? TEXT("yes") : TEXT("no"), bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 4 file"), static_cast<int32>(Stepping),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes 'not marked' into the file"), bWritten && !bSteppedMark);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmCharacterSave::SchemaVersionNow);
	TestFalse(TEXT("a character from before the mark is not marked"), Read->bMarkedByConsumption);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

// ---------------------------------------------------------------------------
// The sheet
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionMarkOnTheSheet,
	"Cataclysm.ConsumptionMark.TheSheetSaysMarkedOnlyForAMarkedCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionMarkOnTheSheet::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a marked character's line"), UCataclysmCharacterSheetLayout::ConsumptionMarkLine(true),
			  FString(TEXT("Marked: crossed the Consumption Threshold")));
	TestEqual(TEXT("an unmarked character has none"), UCataclysmCharacterSheetLayout::ConsumptionMarkLine(false),
			  FString());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
