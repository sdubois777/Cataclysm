// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Items/CataclysmLootFilterPreferences.h"
#include "CataclysmLootFilterWidget.generated.h"

class UCataclysmChoiceButton;
class UEditableTextBox;
class UPanelWidget;
class UTextBlock;

/**
 * The loot filter screen: the filters, the selected filter's rules, and an editor for the selected rule. Issue #2846,
 * slice LF2. Opened with L, or `Cataclysm.EditLootFilters`. It does not pause the game, as no screen in this game does.
 *
 * WHAT A PLAYER DOES ON IT:
 *   - picks a filter in the list, and makes it the one in use ("Use this filter");
 *   - makes a new empty filter, copies any filter (a built-in included), renames one of their own to the name typed in
 *     the box, or deletes one of their own;
 *   - picks a rule, adds one, deletes one, or moves one up or down: the highest rule that matches decides;
 *   - changes the picked rule with the editor's buttons: each condition and the action is a toggle, a cycle or a step.
 * A BUILT-IN FILTER CAN BE USED AND COPIED, NOT CHANGED: every change to one is refused with
 * `UCataclysmLootFilterStore::BuiltInRefusal`, which the refusal line shows.
 *
 * EVERY CHANGE IS WRITTEN TO THE PREFERENCES FILE AT ONCE (the owner's ruling O4), through the game instance.
 *
 * THE LAYOUT IS IN A WIDGET BLUEPRINT AND THE LOGIC IS HERE, as every screen in this project is built.
 * `tools/generate_interface_assets.py` writes the first `WBP_LootFilter`. EVERY CLICK GOES THROUGH `Press`, with the
 * command the button carries, so a headless test -- which has no Widget Blueprint and so no buttons -- presses the
 * same commands. The editor's commands and labels are `UCataclysmLootFilterLayout`'s.
 */
UCLASS()
class CATACLYSM_API UCataclysmLootFilterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

	/**
	 * What a button's click does. The commands:
	 *   "Filter:N"  pick filter N of `UCataclysmLootFilterStore::AllNames`
	 *   "Rule:N"    pick rule N of the picked filter
	 *   "Do:Use", "Do:New", "Do:Copy", "Do:Rename", "Do:Delete"
	 *   "Do:AddRule", "Do:DeleteRule", "Do:Up", "Do:Down"
	 *   "Edit:AffixPick+" / "Edit:AffixPick-"   step the affix chooser
	 *   "Edit:<command>"   `UCataclysmLootFilterLayout::EditRule` on the picked rule
	 * @return whether it did anything; when not, `RefusalText` says why
	 */
	bool Press(const FString& Command);

	/** Read the preferences again and redraw the three panels and the two lines. */
	void Refresh();

	/** The preferences the screen shows: the game instance's, or the test's. */
	const FCataclysmLootFilterPreferences& Preferences() const;

	/** The picked filter's name, and the picked rule's index or INDEX_NONE. */
	const FString& SelectedFilter() const { return Picked; }
	int32 SelectedRule() const { return PickedRule; }

	/** The two lines: what is picked and in use, and why the last press did nothing. */
	const FString& StatusText() const { return LastStatus; }
	const FString& RefusalText() const { return LastRefusal; }

	/** The commands and labels of every button the three panels hold, in order, as the last redraw made them. */
	const TArray<TPair<FString, FString>>& ButtonsShown() const { return Shown; }

	/** The heading. */
	static const TCHAR* TitleText;

	// ----------------------------------------------------------------------
	// Test seams
	// ----------------------------------------------------------------------

	/** Preferences to show when there is no game instance to hold them; never written to a file. */
	void SetPreferencesForTests(const FCataclysmLootFilterPreferences& InPreferences);

	/** The name "Do:Rename" uses when there is no name box: a headless test has none. */
	void SetTypedNameForTests(const FString& Name) { TypedName = Name; }

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> FilterBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> RuleBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> EditorBox;

	/** What is picked and which filter is in use. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusLabel;

	/** Why the last press did nothing. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	/** Where a new name is typed for "Rename to the name typed". */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> NameBox;

	/** The heading. Optional, because a heading is decoration. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/** What one button is drawn as. Soft, for the reason the character sheet's is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

private:
	/** The preferences to change: the game instance's, or the test's. */
	FCataclysmLootFilterPreferences* Editable();

	/** Writes the preferences file, when the preferences are the game instance's. */
	void SaveIfReal();

	/** Keeps the picked filter and rule pointing at something that exists. */
	void KeepPicksValid();

	/** Rebuild `Shown`, and the buttons in the three panels when they exist. */
	void Redraw();

	/** One button into a panel, and its command and label into `Shown`. */
	void AddButton(UPanelWidget* Panel, TSubclassOf<UCataclysmChoiceButton> ButtonClass, const FString& Command,
				   const FString& Label, bool bChosen);

	UFUNCTION()
	void HandleClicked(FName Value);

	FString Picked;
	int32 PickedRule = INDEX_NONE;
	int32 AffixPick = 0;
	TArray<FName> AffixRows;
	FString TypedName;
	FString LastStatus;
	FString LastRefusal;
	TArray<TPair<FString, FString>> Shown;

	bool bUseTestPreferences = false;
	FCataclysmLootFilterPreferences TestPreferences;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> Buttons;
};
