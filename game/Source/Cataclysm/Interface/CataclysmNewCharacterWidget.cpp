// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmNewCharacterWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmCharacterSelectWidget.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CataclysmFrontEndGameMode.h"
#include "Player/CataclysmGameInstance.h"

bool UCataclysmNewCharacterWidget::NameIsAllowed(const FString& Typed, FString& OutName, FString& OutWhy)
{
	OutName = Typed.TrimStartAndEnd();
	const int32 Length = OutName.Len();
	if (Length < LeastNameLength)
	{
		OutWhy = FString::Printf(TEXT("a name has at least %d characters"), LeastNameLength);
		return false;
	}
	if (Length > MostNameLength) // A name's rule: its longest.
	{
		OutWhy = FString::Printf(TEXT("a name has at most %d characters"), MostNameLength);
		return false;
	}
	for (int32 Index = 0; Index < Length; ++Index)
	{
		const TCHAR Each = OutName[Index];
		if (FChar::IsAlpha(Each))
		{
			continue;
		}
		if (Each == TEXT(' ') || Each == TEXT('\'') || Each == TEXT('-'))
		{
			// BETWEEN TWO LETTERS, so never at an end and never two together.
			if (Index == 0 || Index == Length - 1 || !FChar::IsAlpha(OutName[Index - 1]) || !FChar::IsAlpha(OutName[Index + 1]))
			{
				OutWhy = TEXT("a space, an apostrophe or a hyphen goes only between two letters");
				return false;
			}
			continue;
		}
		OutWhy = FString::Printf(TEXT("a name holds letters, and single spaces, apostrophes and hyphens between them; '%c' "
									  "is none of those"), Each);
		return false;
	}
	OutWhy.Empty();
	return true;
}

void UCataclysmNewCharacterWidget::SetName(const FString& Typed)
{
	Name = Typed;
	FString Clean;
	FString Why;
	ShowCreationRefusal(NameIsAllowed(Name, Clean, Why) ? FString() : Why);
}

void UCataclysmNewCharacterWidget::ChooseLethality(ECataclysmLethality Lethality)
{
	Partition.Lethality = Lethality;
	FillTheLockedChoices();
}

void UCataclysmNewCharacterWidget::SetSoloSelfFound(bool bSoloSelfFound)
{
	Partition.bSoloSelfFound = bSoloSelfFound;
}

bool UCataclysmNewCharacterWidget::ChoosePopulation(ECataclysmPopulation Population)
{
	if (Population != ECataclysmPopulation::Offline)
	{
		ShowCreationRefusal(OnlineIsNotBuilt);
		return false;
	}
	Partition.Population = Population;
	FillTheLockedChoices();
	return true;
}

bool UCataclysmNewCharacterWidget::ConfirmInto(UCataclysmGameInstance* Instance)
{
	FString Clean;
	FString Why;
	if (!NameIsAllowed(Name, Clean, Why))
	{
		ShowCreationRefusal(Why);
		return false;
	}
	if (!CanConfirm())
	{
		const FString Refusal = RefusalText().ToString();
		ShowCreationRefusal(Refusal.IsEmpty() ? FString(TEXT("choose a weapon type and a damage type")) : Refusal);
		return false;
	}
	if (Partition.Population != ECataclysmPopulation::Offline)
	{
		ShowCreationRefusal(OnlineIsNotBuilt);
		return false;
	}
	Instance = Instance ? Instance : Cast<UCataclysmGameInstance>(GetGameInstance());
	if (!Instance)
	{
		ShowCreationRefusal(TEXT("there is no game instance of this game's class to hand the character to"));
		return false;
	}

	Instance->PendingCharacterId.Invalidate();
	FCataclysmPendingCreation& Creation = Instance->PendingCreation;
	Creation.Name = Clean;
	Creation.Partition = Partition;
	Creation.WeaponType = GetChoice().WeaponType;
	Creation.DamageType = GetChoice().DamageType;
	Creation.bPending = true; // A new character's: the creation handed over.
	ShowCreationRefusal(FString());

	Requested = UCataclysmCharacterSelectWidget::SandboxMap;
	// NOT IN AN AUTOMATION TEST, which cannot travel; it reads `LevelRequested`.
	if (!GIsAutomationTesting)
	{
		UGameplayStatics::OpenLevel(this, FName(*Requested));
	}
	return true;
}

bool UCataclysmNewCharacterWidget::Confirm()
{
	return ConfirmInto(nullptr);
}

void UCataclysmNewCharacterWidget::GoBack()
{
	if (ACataclysmFrontEndPlayerController* FrontEnd = Cast<ACataclysmFrontEndPlayerController>(GetOwningPlayer()))
	{
		FrontEnd->ShowTheSelectScreen();
	}
}

void UCataclysmNewCharacterWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (NameBox)
	{
		NameBox->OnTextChanged.AddDynamic(this, &UCataclysmNewCharacterWidget::HandleNameChanged);
	}
	if (SoloSelfFoundBox)
	{
		SoloSelfFoundBox->SetIsChecked(Partition.bSoloSelfFound);
		SoloSelfFoundBox->OnCheckStateChanged.AddDynamic(this, &UCataclysmNewCharacterWidget::HandleSoloSelfFoundChanged);
	}
	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &UCataclysmNewCharacterWidget::HandleBackClicked);
	}
	FillTheLockedChoices();
}

void UCataclysmNewCharacterWidget::FillTheLockedChoices()
{
	const TSubclassOf<UCataclysmChoiceButton> ButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}
	const UEnum* Lethalities = StaticEnum<ECataclysmLethality>();
	if (LethalityBox)
	{
		LethalityBox->ClearChildren();
		for (const ECataclysmLethality Each : {ECataclysmLethality::Standard, ECataclysmLethality::Hardcore,
											   ECataclysmLethality::Heretic})
		{
			if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
			{
				Button->SetChoice(FName(*FString::FromInt(static_cast<int32>(Each))),
								  Lethalities->GetDisplayNameTextByValue(static_cast<int64>(Each)),
								  /*bInChosen=*/Partition.Lethality == Each, /*bInAvailable=*/true);
				Button->OnChoiceClicked.AddDynamic(this, &UCataclysmNewCharacterWidget::HandleLethalityClicked);
				LethalityBox->AddChild(Button);
			}
		}
	}
	if (PopulationBox)
	{
		PopulationBox->ClearChildren();
		for (const ECataclysmPopulation Each : {ECataclysmPopulation::Offline, ECataclysmPopulation::Online})
		{
			if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
			{
				const bool bOffline = Each == ECataclysmPopulation::Offline;
				Button->SetChoice(FName(*FString::FromInt(static_cast<int32>(Each))),
								  FText::FromString(bOffline ? TEXT("Offline") : FString::Printf(TEXT("Online: %s"), OnlineIsNotBuilt)),
								  /*bInChosen=*/Partition.Population == Each, /*bInAvailable=*/bOffline);
				Button->OnChoiceClicked.AddDynamic(this, &UCataclysmNewCharacterWidget::HandlePopulationClicked);
				PopulationBox->AddChild(Button);
			}
		}
	}
}

void UCataclysmNewCharacterWidget::HandleNameChanged(const FText& Text)
{
	SetName(Text.ToString());
}

void UCataclysmNewCharacterWidget::HandleSoloSelfFoundChanged(bool bIsChecked)
{
	SetSoloSelfFound(bIsChecked);
}

void UCataclysmNewCharacterWidget::HandleLethalityClicked(FName Value)
{
	ChooseLethality(static_cast<ECataclysmLethality>(FCString::Atoi(*Value.ToString())));
}

void UCataclysmNewCharacterWidget::HandlePopulationClicked(FName Value)
{
	ChoosePopulation(static_cast<ECataclysmPopulation>(FCString::Atoi(*Value.ToString())));
}

void UCataclysmNewCharacterWidget::HandleBackClicked()
{
	GoBack();
}

void UCataclysmNewCharacterWidget::ShowCreationRefusal(const FString& Why)
{
	CreationRefusal = Why;
	if (CreationRefusalLabel)
	{
		CreationRefusalLabel->SetText(FText::FromString(Why));
	}
}
