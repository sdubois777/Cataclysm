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
	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());
}
