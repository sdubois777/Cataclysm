// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "CataclysmChoicePanelWidget.generated.h"

class ACataclysmFloorObject;
class UCataclysmChoiceButton;
class UPanelWidget;
class UTextBlock;

/**
 * The panel a floor object opens when the player reaches it: its name, what the choice is about, one button per
 * choice and "Leave". Issues #1820 and #41, the interaction and choice screen.
 *
 * IT DOES NOT PAUSE THE GAME, like every other screen in the project; creatures go on while it is open. It is laid
 * out by `WBP_ChoicePanel`, which `tools/generate_interface_assets.py` builds.
 *
 * A CHOICE IS THE DUNGEON GAME MODE'S, not this panel's: pressing one calls
 * `ACataclysmDungeonGameMode::ChooseAtFloorObject`, and the panel then closes. "Leave" closes it and chooses nothing.
 */
UCLASS()
class CATACLYSM_API UCataclysmChoicePanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The key of the button that closes the panel and chooses nothing. */
	static const FName LeaveKey;

	/** Which object's choices to show, and show them. */
	void SetObject(ACataclysmFloorObject* Object);

	/** The object shown, or null once it is gone. */
	ACataclysmFloorObject* ShownObject() const { return Shown.Get(); }

	/** The keys of the buttons, in order, "Leave" last. For tests, which have no Blueprint and so no buttons. */
	const TArray<FName>& ChoiceKeys() const { return Keys; }

	/** Whether a press of the last choice acted, for tests. */
	bool LastChoiceActed() const { return bLastActed; }

	/** Press the button of this key, through the same handler a button calls. */
	void ChooseForTests(FName ChoiceKey);

	/** Take the panel off the screen, if it is on it. */
	void Close();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PromptLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ChoiceBox;

	/** Which Widget Blueprint one choice button is. Soft, for the reason the city screen's is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Dungeon")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

private:
	void Refresh();

	UFUNCTION()
	void HandleChoiceClicked(FName Value);

	TWeakObjectPtr<ACataclysmFloorObject> Shown;
	TArray<FName> Keys;
	bool bLastActed = false;
};
