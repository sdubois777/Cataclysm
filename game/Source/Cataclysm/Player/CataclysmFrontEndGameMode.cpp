// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmFrontEndGameMode.h"

#include "Cataclysm.h"
#include "Interface/CataclysmCharacterSelectWidget.h"
#include "Interface/CataclysmNewCharacterWidget.h"
#include "Player/CataclysmPlayerController.h"

ACataclysmFrontEndGameMode::ACataclysmFrontEndGameMode()
{
	PlayerControllerClass = ACataclysmFrontEndPlayerController::StaticClass();
	bStartPlayersAsSpectators = true;
}

void ACataclysmFrontEndPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	ShowTheSelectScreen();
}

UCataclysmCharacterSelectWidget* ACataclysmFrontEndPlayerController::SelectScreen() const
{
	return Cast<UCataclysmCharacterSelectWidget>(Shown);
}

void ACataclysmFrontEndPlayerController::ShowTheSelectScreen()
{
	ShowOnly(SelectScreenClass.LoadSynchronous(), SelectScreenClass.ToString());
}

void ACataclysmFrontEndPlayerController::ShowTheNewCharacterScreen()
{
	ShowOnly(NewCharacterScreenClass.LoadSynchronous(), NewCharacterScreenClass.ToString());
}

void ACataclysmFrontEndPlayerController::ShowOnly(UClass* ScreenClass, const FString& Path)
{
	if (!ScreenClass)
	{
		UE_LOG(LogCataclysm, Warning, TEXT("A front end screen's asset is missing: %s"), *Path);
		return;
	}
	if (Shown)
	{
		Shown->RemoveFromParent();
		Shown = nullptr;
	}
	Shown = CreateWidget<UUserWidget>(this, ScreenClass);
	if (!Shown)
	{
		return;
	}
	Shown->AddToViewport();
	UseTheMenuInputMode(Shown);
}

FString ACataclysmFrontEndPlayerController::UseTheMenuInputMode(UUserWidget* Menu)
{
	// THE CURSOR SHOWN AND CLICKS TAKEN, AND THE MODE SET BY THE GAME'S ONE FUNCTION FOR IT, issue #1015 and
	// `tools/tests/test_one_input_mode.py`; this controller is not the game's, so it calls the function's static form.
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	ACataclysmPlayerController::ApplyPlayingInputModeTo(*this, Menu, &AskedFor); // The front end's: the menu mode.
	return AskedFor;
}
