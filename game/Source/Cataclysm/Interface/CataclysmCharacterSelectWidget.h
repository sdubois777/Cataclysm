// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Save/CataclysmSaveLoad.h"
#include "CataclysmCharacterSelectWidget.generated.h"

class UButton;
class UCataclysmChoiceButton;
class UCataclysmGameInstance;
class UEditableTextBox;
class UPanelWidget;
class UTextBlock;

/**
 * The character select screen: the saved characters, newest first, each one a button that continues it, and a button
 * for a new character. Issue #753, select-screen step 3a, ruled 2026-10-31.
 *
 * THE LOGIC HERE AND THE LAYOUT IN `WBP_CharacterSelect`, the rule of 2026-08-24; the asset is made in step 3b. Every
 * bound pointer may be null, which is what a test that builds this with `NewObject` has.
 *
 * CONTINUE HANDS THE CHOICE TO THE GAME INSTANCE AND OPENS THE LEVEL ITS RUN NEEDS: a run saved inside a dungeon is
 * loaded only in a dungeon level, so the row's dungeon decides. The level, when it begins, loads the character
 * (`ACataclysmGameMode::LoadThePendingCharacter`); a refusal comes back here with its reason, which this shows.
 */
UCLASS()
class CATACLYSM_API UCataclysmCharacterSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SandboxMap = TEXT("/Game/Maps/L_Sandbox");
	static constexpr const TCHAR* DungeonMap = TEXT("/Game/Maps/L_Dungeon");

	/**
	 * One row's text, as ruling 2 lists it: the name, the level, the lethality, the Solo Self-Found and population flags,
	 * the weapon type, and "in a run: day N", with "floor F of <dungeon>" when the run was saved inside a dungeon. An
	 * unreadable slot says so, with why.
	 */
	static FString RowText(const FCataclysmCharacterSummary& Row);

	/**
	 * The level Continue opens for a row: the dungeon level for a run saved on a floor of a dungeon other than the
	 * sandbox, else the sandbox. False, with why, for a row whose record cannot be read.
	 */
	static bool MapToContinue(const FCataclysmCharacterSummary& Row, FString& OutMap, FString& OutWhy);

	/** Reads the saved characters again and shows them; shows, and clears, the reason the last load was refused. */
	void Refresh();

	const TArray<FCataclysmCharacterSummary>& Rows() const { return Shown; }

	/**
	 * Continue the character in row `Index`: the game instance holds it pending and its level is opened. False, with the
	 * reason shown, when it cannot be.
	 *
	 * @param Instance  the game instance to hand it to; the one this screen belongs to when null
	 */
	bool ContinueAt(int32 Index, UCataclysmGameInstance* Instance = nullptr);

	/**
	 * A new character: refused when the account holds `MostCharacters`, counting an unreadable slot, which still holds one
	 * of them; otherwise nothing pending, and the front end shows the new-character screen. Select-screen step 4a. When
	 * that screen's asset does not load, refused with `NewCharacterScreenMissing`, and this screen stays.
	 */
	bool StartNew(UCataclysmGameInstance* Instance = nullptr);

	/** New's refusal when the new-character screen's asset does not load, as between steps 4a and 4b. */
	static constexpr const TCHAR* NewCharacterScreenMissing =
		TEXT("the new-character screen could not be opened: its asset is missing");

	/** "An account holds 24 characters, as one pool", `docs/Cataclysm_GDD_v2.md`. Nothing raises it. */
	static constexpr int32 MostCharacters = 24;

	/** Whether an account listing this many characters may make another. */
	static bool HasRoomForANewCharacter(int32 Listed) { return Listed < MostCharacters; } // The account's: 24 at most.

	/** Whether the last New asked for the new-character screen. */
	bool NewCharacterRequested() const { return bNewCharacterRequested; }

	FString RefusalText() const { return Refusal; }

	/** The level the last Continue or New asked to open. Travel does not run in an automation test, so a test reads this. */
	FString LevelRequested() const { return Requested; }

	/** For tests: the rows, as `Refresh` would read them. */
	void SetRowsForTests(const TArray<FCataclysmCharacterSummary>& InRows) { Shown = InRows; }

	// --- Deleting a character, select-screen step 5, ruled 2026-11-01 ---------------------------------------------------

	/** What is typed to confirm deleting a save that has no name to type: an unreadable one, or an unnamed one. */
	static constexpr const TCHAR* DeleteWord = TEXT("DELETE");

	/**
	 * What must be typed to delete `Row`'s character: its name, as Diablo IV asks, or `DeleteWord` when it has none.
	 */
	static FString ConfirmationWordFor(const FCataclysmCharacterSummary& Row);

	/** Whether `Typed` confirms deleting `Row`'s character: exactly its confirmation word, case and all. */
	static bool ConfirmationMatches(const FCataclysmCharacterSummary& Row, const FString& Typed);

	/**
	 * The confirmation's text: who is deleted and that it cannot be undone; its run, when it is in one (ruling 6); for a
	 * Solo Self-Found character, that its own empire upgrade tree and private stash go with it (rulings 4 and 5); for an
	 * unreadable save, that none of that can be told, that any run it had stays, and that its backups are kept (Q2).
	 */
	static FString DeleteWarning(const FCataclysmCharacterSummary& Row);

	/** What a click on row `Index` does: in delete mode it asks to delete that character, otherwise it continues it. */
	void ClickRow(int32 Index);

	/** Delete mode on or off. In it a row asks to delete its character instead of continuing it. Closes any confirmation. */
	void SetDeleteMode(bool bOn);
	bool IsInDeleteMode() const { return bDeleteMode; }

	/** Opens the confirmation for row `Index`, nothing typed yet. False for a row that does not exist. */
	bool AskToDeleteAt(int32 Index);

	/** What the player has typed in the confirmation; Delete is enabled only while it matches. */
	void SetConfirmationTyped(const FString& Typed);

	/** Whether a confirmation is open and what was typed matches its row. */
	bool CanConfirmDelete() const;

	/**
	 * Deletes the confirmed row's character with `FCataclysmSaveLoad::DeleteCharacter`, then leaves delete mode and reads
	 * the saves again. Refused, deleting nothing, when what was typed does not match.
	 */
	bool ConfirmDelete();

	/** Closes the confirmation; delete mode stays as it was. */
	void CancelDelete();

	int32 PendingDeleteIndex() const { return PendingDelete; }
	FString ConfirmationText() const { return Confirmation; }

	/** The slots the last delete removed. */
	const TArray<FString>& LastDeleted() const { return Deleted; }

protected:
	virtual void NativeConstruct() override;

	/** The rows, one `WBP_ChoiceButton` each. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> CharacterList;

	/** Why a load or a Continue was refused; empty otherwise. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> NewButton;

	/** OPTIONAL, BECAUSE IT IS A LABEL ONLY and the screen works without it. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/** Delete mode on and off. Select-screen step 5. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> DeleteButton;

	/** The confirmation, hidden until a row asks to delete its character. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ConfirmPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ConfirmText;

	/** Where the character's name, or `DeleteWord`, is typed. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> ConfirmNameBox;

	/** Enabled only while what is typed matches. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ConfirmDeleteButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

private:
	UFUNCTION()
	void HandleRowClicked(FName Value);

	UFUNCTION()
	void HandleNewClicked();

	UFUNCTION()
	void HandleDeleteClicked();

	UFUNCTION()
	void HandleConfirmDeleteClicked();

	UFUNCTION()
	void HandleCancelClicked();

	UFUNCTION()
	void HandleConfirmNameChanged(const FText& Text);

	/** The rows as buttons, from `Shown`: every row clickable in delete mode, only the readable ones otherwise. */
	void ShowRows();

	/** The confirmation shown or hidden, and Delete enabled as `CanConfirmDelete` says. */
	void ShowConfirmation();

	UCataclysmGameInstance* OwnInstance() const;
	void ShowRefusal(const FString& Why);
	void Open(const FString& Map);

	TArray<FCataclysmCharacterSummary> Shown;
	FString Refusal;
	FString Requested;
	bool bNewCharacterRequested = false;
	bool bDeleteMode = false;
	int32 PendingDelete = INDEX_NONE;
	FString Typed;
	FString Confirmation;
	TArray<FString> Deleted;
};
