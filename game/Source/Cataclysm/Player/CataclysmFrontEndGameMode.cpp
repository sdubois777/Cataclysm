// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmFrontEndGameMode.h"

#include "Cataclysm.h"
#include "Interface/CataclysmCharacterSelectWidget.h"

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

	// THE CURSOR SHOWN AND CLICKS TAKEN, AND NO INPUT MODE SET HERE: the game sets one mode, in one place,
	// `ACataclysmPlayerController::ApplyPlayingInputMode` (issue #1015, `tools/tests/test_one_input_mode.py`), and this
	// controller is not that one. Whether the screen's buttons take clicks under the engine's default mode is for the
	// owner's launch to show; no automation test can see a cursor.
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}
