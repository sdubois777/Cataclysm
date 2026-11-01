// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCharacterSelectWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CataclysmFrontEndGameMode.h"
#include "Player/CataclysmGameInstance.h"
#include "Save/CataclysmSaveWriter.h"

FString UCataclysmCharacterSelectWidget::RowText(const FCataclysmCharacterSummary& Row)
{
	if (!Row.bReadable)
	{
		return FString::Printf(TEXT("Unreadable save, %s: %s"), *Row.SlotName, *Row.Why);
	}

	const UEnum* Lethality = StaticEnum<ECataclysmLethality>();
	const UEnum* Population = StaticEnum<ECataclysmPopulation>();
	FString Run = TEXT("no run");
	if (Row.bInARun && !Row.bRunReadable)
	{
		Run = TEXT("a run that cannot be read");
	}
	else if (Row.bInARun)
	{
		Run = FString::Printf(TEXT("in a run: day %d"), Row.Day);
		if (Row.Floor > 0)
		{
			Run += FString::Printf(TEXT(", floor %d of %s"), Row.Floor, *Row.Dungeon.ToString());
		}
	}
	return FString::Printf(TEXT("%s, level %d, %s%s, %s, %s, %s"),
						   Row.Name.IsEmpty() ? TEXT("(unnamed)") : *Row.Name, Row.Level,
						   *Lethality->GetDisplayNameTextByValue(static_cast<int64>(Row.Partition.Lethality)).ToString(),
						   Row.Partition.bSoloSelfFound ? TEXT(", Solo Self-Found") : TEXT(""),
						   *Population->GetDisplayNameTextByValue(static_cast<int64>(Row.Partition.Population)).ToString(),
						   Row.WeaponType.IsEmpty() ? TEXT("no weapon type") : *Row.WeaponType, *Run);
}

bool UCataclysmCharacterSelectWidget::MapToContinue(const FCataclysmCharacterSummary& Row, FString& OutMap,
													 FString& OutWhy)
{
	if (!Row.bReadable)
	{
		OutWhy = FString::Printf(TEXT("this save cannot be read: %s"), *Row.Why);
		return false;
	}
	// A RUN SAVED INSIDE A DUNGEON LOADS ONLY IN A DUNGEON LEVEL (`FCataclysmSaveLoad::CharacterAndRun` refuses it
	// anywhere else); every other run, and a character in none, loads in the sandbox.
	const bool bInADungeon = Row.bInARun && Row.bRunReadable && Row.Floor > 0 && Row.Dungeon != FName(TEXT("Sandbox"));
	OutMap = bInADungeon ? DungeonMap : SandboxMap; // A Continue's: the level its run needs.
	return true;
}

void UCataclysmCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (NewButton)
	{
		NewButton->OnClicked.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleNewClicked);
	}
	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(TEXT("Choose a character")));
	}
	Refresh();
}

void UCataclysmCharacterSelectWidget::Refresh()
{
	Shown = FCataclysmSaveLoad::SavedCharacters(UCataclysmSaveWriter::UserIndex);

	// THE REASON THE LAST LOAD WAS REFUSED, shown once.
	if (UCataclysmGameInstance* Instance = OwnInstance(); Instance && !Instance->LastLoadRefusal.IsEmpty())
	{
		ShowRefusal(Instance->LastLoadRefusal);
		Instance->LastLoadRefusal.Empty();
	}

	if (!CharacterList)
	{
		return;
	}
	CharacterList->ClearChildren();
	const TSubclassOf<UCataclysmChoiceButton> ButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}
	for (int32 Index = 0; Index < Shown.Num(); ++Index)
	{
		if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Button->SetChoice(FName(*FString::FromInt(Index)), FText::FromString(RowText(Shown[Index])),
							  /*bInChosen=*/false, /*bInAvailable=*/Shown[Index].bReadable);
			Button->OnChoiceClicked.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleRowClicked);
			CharacterList->AddChild(Button);
		}
	}
}

bool UCataclysmCharacterSelectWidget::ContinueAt(int32 Index, UCataclysmGameInstance* Instance)
{
	if (!Shown.IsValidIndex(Index))
	{
		ShowRefusal(TEXT("there is no such character"));
		return false;
	}
	FString Map;
	FString Why;
	if (!MapToContinue(Shown[Index], Map, Why))
	{
		ShowRefusal(Why);
		return false;
	}
	Instance = Instance ? Instance : OwnInstance();
	if (!Instance)
	{
		ShowRefusal(TEXT("there is no game instance of this game's class to hand the character to"));
		return false;
	}
	Instance->PendingCharacterId = Shown[Index].CharacterId; // A Continue's: the character handed over.
	Instance->LastLoadRefusal.Empty();
	Open(Map);
	return true;
}

bool UCataclysmCharacterSelectWidget::StartNew(UCataclysmGameInstance* Instance)
{
	bNewCharacterRequested = false;
	if (!HasRoomForANewCharacter(Shown.Num()))
	{
		ShowRefusal(FString::Printf(TEXT("this account holds %d characters, the most it may; delete one to make another"),
									MostCharacters));
		return false;
	}
	Instance = Instance ? Instance : OwnInstance();
	if (Instance)
	{
		Instance->PendingCharacterId.Invalidate();
	}

	// THE NEW-CHARACTER SCREEN, in this screen's place. Select-screen step 4a.
	bNewCharacterRequested = true;
	if (ACataclysmFrontEndPlayerController* FrontEnd = Cast<ACataclysmFrontEndPlayerController>(GetOwningPlayer());
		FrontEnd && !FrontEnd->ShowTheNewCharacterScreen())
	{
		// THE ASSET MISSING, as between steps 4a and 4b: this screen stays, and says why.
		bNewCharacterRequested = false;
		ShowRefusal(NewCharacterScreenMissing);
		return false;
	}
	return true;
}

void UCataclysmCharacterSelectWidget::HandleRowClicked(FName Value)
{
	ContinueAt(FCString::Atoi(*Value.ToString()));
}

void UCataclysmCharacterSelectWidget::HandleNewClicked()
{
	StartNew();
}

UCataclysmGameInstance* UCataclysmCharacterSelectWidget::OwnInstance() const
{
	return Cast<UCataclysmGameInstance>(GetGameInstance());
}

void UCataclysmCharacterSelectWidget::ShowRefusal(const FString& Why)
{
	Refusal = Why;
	if (RefusalLabel)
	{
		RefusalLabel->SetText(FText::FromString(Why));
	}
}

void UCataclysmCharacterSelectWidget::Open(const FString& Map)
{
	Requested = Map;
	// NOT IN AN AUTOMATION TEST, which cannot travel; it reads `LevelRequested`.
	if (!GIsAutomationTesting)
	{
		UGameplayStatics::OpenLevel(this, FName(*Map));
	}
}
