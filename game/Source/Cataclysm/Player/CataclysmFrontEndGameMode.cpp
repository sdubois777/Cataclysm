// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmFrontEndGameMode.h"

#include "Cataclysm.h"
#include "Interface/CataclysmCharacterSelectWidget.h"
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
	const TSubclassOf<UCataclysmCharacterSelectWidget> ScreenClass = SelectScreenClass.LoadSynchronous();
	if (!ScreenClass)
	{
		UE_LOG(LogCataclysm, Warning, TEXT("The character select screen's asset is missing: %s"),
			   *SelectScreenClass.ToString());
		return;
	}
	Screen = CreateWidget<UCataclysmCharacterSelectWidget>(this, ScreenClass);
	if (!Screen)
	{
		return;
	}
	Screen->AddToViewport();
	UseTheMenuInputMode(Screen);
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
