// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "CataclysmMerchantWidget.generated.h"

class UButton;
class UCataclysmChoiceButton;
class UPanelWidget;
class UTextBlock;

/**
 * The merchant's screen: the gold held, one button for each carried piece of gear with what it sells for, a Sell button
 * that asks first, and Close. Issue #48, slice S1. Opened by the empire map's "Merchant" button, beside the Forge's (the
 * owner's O5 of 2026-11-22, "Screen from the empire map (Recommended)").
 *
 * ASK, THEN SELL: the first press of Sell says what the chosen piece sells for and asks; the second sells it. Choosing
 * another piece cancels the question. A sale costs no day (the owner's O2).
 *
 * ONLY CARRIED PIECES ARE LISTED (`UCataclysmMerchant::CarriedPiecesOf`), so a worn piece cannot be chosen. Materials
 * and gems are not listed.
 *
 * THE LAYOUT IS IN A WIDGET BLUEPRINT AND THE LOGIC IS HERE, as every screen in this project is built.
 * `tools/generate_interface_assets.py` writes the first `WBP_Merchant`. EVERY CLICK GOES THROUGH `Press`, so a headless
 * test, which has no Blueprint and so no buttons, presses the same commands.
 */
UCLASS()
class CATACLYSM_API UCataclysmMerchantWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

	/**
	 * What a button's click does. "Cell:N" chooses the piece in cell N of the bag; "Sell" asks, then sells; "Close"
	 * closes the screen.
	 * @return whether it did anything
	 */
	bool Press(const FString& Command);

	/** Read the character again and redraw the gold line, the pieces and the Sell button. */
	void Refresh();

	/** The world to act in when there is no owning player: a headless test's. */
	void SetWorldContextForTests(UObject* Context) { WorldContextForTests = Context; }

	/** The cell chosen, or INDEX_NONE. */
	int32 ChosenCell() const { return Chosen; }

	/** Whether the Sell button has asked and waits for its second press. */
	bool IsAsking() const { return bAsked; }

	/** The gold line, the chosen piece's line and the last message, as the screen shows them. */
	const FString& GoldText() const { return GoldLine; }
	const FString& DetailText() const { return Detail; }
	const FString& MessageText() const { return Message; }

	/** The commands and labels of every button the last redraw made, in order. */
	const TArray<TPair<FString, FString>>& ButtonsShown() const { return Shown; }

	/** The heading, the Sell button's word, and the question the first press asks. */
	static const TCHAR* TitleText;
	static const TCHAR* SellText;
	static const TCHAR* ChooseFirstText;

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleLabel;

	/** The gold held. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GoldLabel;

	/** One button for each carried piece. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> PieceBox;

	/** The chosen piece and what it sells for. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailLabel;

	/** The Sell button. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ActionBox;

	/** What the last press did, or why it did nothing. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	/** What one button is drawn as. Soft, for the reason the character sheet's is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Merchant")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

private:
	/** The world to act in: the owning player, or the test's. */
	const UObject* Context() const;

	/** One button into a panel, and its command and label into `Shown`. */
	void AddButton(UPanelWidget* Panel, TSubclassOf<UCataclysmChoiceButton> ButtonClass, const FString& Command,
				   const FString& Label, bool bChosen);

	UFUNCTION()
	void HandleClicked(FName Value);

	UFUNCTION()
	void HandleCloseClicked();

	int32 Chosen = INDEX_NONE;
	bool bAsked = false;
	bool bCloseBound = false;
	FString GoldLine;
	FString Detail;
	FString Message;
	TArray<TPair<FString, FString>> Shown;

	UPROPERTY(Transient)
	TObjectPtr<UObject> WorldContextForTests;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> Buttons;
};
