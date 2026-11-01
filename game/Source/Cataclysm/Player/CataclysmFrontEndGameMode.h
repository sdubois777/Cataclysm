// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "CataclysmFrontEndGameMode.generated.h"

class UCataclysmCharacterSelectWidget;

/**
 * The game mode of the character select screen's level, `L_FrontEnd`. Issue #753, select-screen step 3a, ruled
 * 2026-10-31; the level is made in step 3b.
 *
 * NOT `ACataclysmGameMode`, ON PURPOSE: that one spawns the sandbox's creatures and begins saving a run, and the select
 * screen does neither. The player starts as a spectator, with no character, and its controller shows the screen.
 */
UCLASS()
class CATACLYSM_API ACataclysmFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACataclysmFrontEndGameMode();
};

/** The select screen's player: it shows `WBP_CharacterSelect` and takes the mouse. Step 3a. */
UCLASS()
class CATACLYSM_API ACataclysmFrontEndPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UCataclysmCharacterSelectWidget* SelectScreen() const { return Screen; }

	/**
	 * The menu's input mode, through the game's one function for it (`ACataclysmPlayerController::
	 * ApplyPlayingInputModeTo`, issue #1015), the cursor shown and clicks taken. Returns, and keeps, the mode asked for.
	 * Called by `BeginPlay` with the screen; public so a test can call it with a widget it built.
	 */
	FString UseTheMenuInputMode(class UUserWidget* Menu);

	/** The mode `UseTheMenuInputMode` last asked for, or empty. */
	FString InputModeAskedFor() const { return AskedFor; }

	/** The screen's asset, which a test loads. Select-screen step 3b. */
	const TSoftClassPtr<UCataclysmCharacterSelectWidget>& SelectScreenAsset() const { return SelectScreenClass; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmCharacterSelectWidget> SelectScreenClass =
		TSoftClassPtr<UCataclysmCharacterSelectWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_CharacterSelect.WBP_CharacterSelect_C")));

private:
	UPROPERTY()
	TObjectPtr<UCataclysmCharacterSelectWidget> Screen;

	FString AskedFor;
};
