// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interface/CataclysmForgeLayout.h"
#include "CataclysmForgeWidget.generated.h"

class UButton;
class UCataclysmChoiceButton;
class UPanelWidget;
class UTextBlock;

/**
 * The Cataclysmic Forge's screen: every worn and carried piece of gear, the piece chosen and what raising it takes, an
 * Upgrade button, and a Combine button for each level of upgrade stone held. The owner's ruling of 2026-11-19, "build
 * it next"; the coordinating session's rulings of the same day, g and h.
 *
 * THE CLASS TREE SCREEN'S SHAPE: the logic and state here in C++, where a headless test reaches them, and the layout in
 * `WBP_Forge`, made by `tools/generate_interface_assets.py`. Every word is `UCataclysmForgeLayout`'s and every craft is
 * `UCataclysmForgeCraft`'s, which saves, passes the days and says the PLAY LOOP lines.
 *
 * OPENED FROM THE EMPIRE MAP'S "Forge" BUTTON, until the capital's Smith exists. It opens during a walk too, to be read;
 * every craft there is refused with `UCataclysmForgeCraft::WalkRefusal` (ruling g).
 *
 * AN UPGRADE TAKES TWO PRESSES (ruling h): the first says what it costs ("Press again to upgrade to +3: ..."), the
 * second does it, and any other press cancels. A worn piece whose upgrade would cross the Consumption Threshold asks
 * that question instead on the second press, and a third does it (ruling e). A Combine is one press: it changes no item
 * and costs no day.
 *
 * EVERY FUNCTION WORKS WITH NO WIDGETS AT ALL, for the class tree screen's reason: the automation command passes
 * `-nullrhi`, so a test can make this class but not its Blueprint, and every bound pointer is null in a test.
 */
UCLASS()
class CATACLYSM_API UCataclysmForgeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

	/** Rebuild every list and line from the character's gear and stones. Keeps the piece chosen while it is there. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Forge")
	void RefreshDisplay();

	// ----------------------------------------------------------------------
	// Test seams
	// ----------------------------------------------------------------------

	/** The world a test's character plays in, used in place of this widget's own, which a headless test lacks. */
	void SetWorldContextForTests(UObject* Context) { WorldContextForTests = Context; }

	/** The pieces listed, as the screen lists them. */
	const TArray<FCataclysmForgeEntry>& Entries() const { return ShownEntries; }

	/** The combine levels shown. */
	const TArray<int32>& CombineLevels() const { return ShownCombineLevels; }

	/** The piece chosen, and whether one is. */
	bool HasChosen() const { return bHasChosen; }
	FCataclysmForgeTarget Chosen() const { return ChosenTarget; }

	FString StonesText() const { return Stones; }
	FString DetailText() const { return Detail; }
	FString MessageText() const { return Message; }
	FString UpgradeText() const { return UpgradeWords; }

	/** The handlers the buttons call, not copies of them. */
	void ClickPieceForTests(FName ButtonName) { HandlePieceClicked(ButtonName); }
	void ClickUpgradeForTests() { HandleActionClicked(UCataclysmForgeLayout::UpgradeButtonName()); }
	void ClickCombineForTests(int32 Level) { HandleActionClicked(UCataclysmForgeLayout::CombineButtonNameFor(Level)); }
	void ClickSocketActionForTests(FName ButtonName) { HandleActionClicked(ButtonName); }

	/** The socket actions shown for the piece chosen. Issue #46, slice G4. */
	const TArray<FCataclysmForgeSocketAction>& SocketActionsShown() const { return ShownSocketActions; }

	/** The socket additions and type changes shown for the piece chosen. Issue #46, slice G9. */
	const TArray<FCataclysmForgeSocketWorkAction>& SocketWorkActionsShown() const { return ShownSocketWorkActions; }

	/** The gem combines shown, and a press of one. Issue #46, slice G6. */
	const TArray<FCataclysmForgeGemCombineAction>& GemCombinesShown() const { return ShownGemCombines; }
	void ClickGemCombineForTests(FName ButtonName) { HandleActionClicked(ButtonName); }

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleLabel;

	/** The upgrade stones held. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StonesLabel;

	/** One button per piece, filled at run time. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> PieceBox;

	/** The piece chosen and what raising it takes. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailLabel;

	/** The Upgrade button and one Combine button per level held, filled at run time. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ActionBox;

	/** A refusal, a question asked before a press, or what a press did. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageLabel;

	/** Closes the screen (`ACataclysmPlayerController::ToggleForge`). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Forge")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

private:
	UFUNCTION()
	void HandlePieceClicked(FName ButtonName);

	UFUNCTION()
	void HandleActionClicked(FName ButtonName);

	UFUNCTION()
	void HandleCloseClicked();

	/** The press that would do the upgrade, after the first asked; any other press cancels it. */
	void CancelThePendingUpgrade();

	/** The world context crafts are done in: the test's, or this widget's own. */
	const UObject* Context() const;

	void BuildPieceButtons();
	void BuildActionButtons();
	void WriteLines();

	TArray<FCataclysmForgeEntry> ShownEntries;
	TArray<int32> ShownCombineLevels;
	TArray<FCataclysmForgeSocketAction> ShownSocketActions;
	TArray<FCataclysmForgeSocketWorkAction> ShownSocketWorkActions;
	TArray<FCataclysmForgeGemCombineAction> ShownGemCombines;

	/** The socket action whose first press was made; its second press does it. None for none. Issue #46, slice G4. */
	FName PendingSocketAction;
	FCataclysmForgeTarget ChosenTarget;
	bool bHasChosen = false;

	/** The first press of Upgrade was made for the piece chosen; the next does it. */
	bool bUpgradeAsked = false;

	/** The threshold question was asked for the piece chosen; the next press crosses. */
	bool bThresholdAsked = false;

	FString Stones;
	FString Detail;
	FString Message;
	FString UpgradeWords;

	UPROPERTY(Transient)
	TObjectPtr<UObject> WorldContextForTests;
};
