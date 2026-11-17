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

bool ACataclysmFrontEndPlayerController::ShowTheNewCharacterScreen()
{
	return ShowOnly(NewCharacterScreenClass.LoadSynchronous(), NewCharacterScreenClass.ToString());
}

bool ACataclysmFrontEndPlayerController::ShowOnly(UClass* ScreenClass, const FString& Path)
{
	if (!ScreenClass) // A front end screen's: a missing asset replaces nothing.
	{
		UE_LOG(LogCataclysm, Warning, TEXT("A front end screen's asset is missing: %s"), *Path);
		return false;
	}
	if (Shown)
	{
		Shown->RemoveFromParent();
		Shown = nullptr;
	}
	Shown = CreateWidget<UUserWidget>(this, ScreenClass);
	if (!Shown)
	{
		return false;
	}
	Shown->AddToViewport();
	UseTheMenuInputMode(Shown);
	return true;
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
