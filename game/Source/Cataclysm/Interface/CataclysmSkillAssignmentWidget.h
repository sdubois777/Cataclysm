// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "CataclysmSkillAssignmentWidget.generated.h"

class ACataclysmPlayerCharacter;
class ACataclysmPlayerState;
class UButton;
class UCataclysmChoiceButton;
class UCataclysmWeaponSlotsComponent;
class UPanelWidget;
class UTextBlock;

/**
 * One of the six keys as the skill-choice screen shows it. Issue #837 layer 4.
 *
 * A PLAIN STRUCT, read by the screen and by its tests, never by a Blueprint.
 */
struct CATACLYSM_API FCataclysmSkillScreenRow
{
	/** The key. One of the six `UCataclysmSkillBar::SlotsShown` gives. */
	ECataclysmAbilitySlot Key = ECataclysmAbilitySlot::None;

	/** The name of the skill on the key, or the skill bar's name for an empty key. */
	FString HeldName;

	/** The row of the skill on the key, or None for an empty key. */
	FName HeldRow;

	/** Whether the skill on the key is the player's choice rather than the key's default. */
	bool bChosen = false;

	/**
	 * A choice kept while its weapon is off (the owner's Q5: "the slot shows its default and the choice is kept for
	 * when the weapon returns"), or None. The key shows its default meanwhile.
	 */
	FName KeptSkillRow;

	/** The line shown under the key for a kept choice, or empty. See `KeptTextFor`. */
	FString KeptText;

	/** What the key's button says: "Heavy: Molten Cleave (default)". */
	FString AsLine() const;
};

/**
 * The screen a player chooses which skill goes on which key: the six keys, the pool the worn weapons make, and a
 * "clear the slot" control. Issue #837 layer 4, ruled 2026-10-25.
 *
 * WHAT IT IS FOR. The design: "The equipped weapons determine the **pool** of skills a player can draw from, not the
 * contents of each slot. ... The player then chooses which skills to use and assigns them to slots." Layer 3 built
 * the choice (`ACataclysmPlayerState::AssignSkill`); until this, only the console could make one.
 *
 * CLICK A SLOT, THEN A SKILL. A skill clicked with no slot selected is refused with "Click a slot first, then a
 * skill." Every other refusal is the player state's own sentence (`RefusalForSkillChoice`), shown as it is: a skill
 * already chosen for another key is refused, as layer 3's O4 rules, and not swapped (ruled Q3).
 *
 * IT DOES NOT PAUSE, and it opens anywhere, a dungeon included (ruled Q2). Safe, because a skill a choice moves takes
 * its running cooldown with it (ruled Q3a; `UCataclysmAbilitySystemComponent::CarryCooldownsToNewKeys`).
 *
 * THE LAYOUT IS IN A WIDGET BLUEPRINT AND THE LOGIC IS HERE, as every screen in this project is built.
 * `tools/generate_interface_assets.py` writes the first `WBP_SkillAssignment` and refuses to touch it afterwards.
 *
 * EVERY CLICK IS RESOLVED AGAINST THE MODEL, NEVER AGAINST THE BUTTONS: a key by its name, a skill by its row. A
 * headless test has no Widget Blueprint, so the panels and the buttons in them never exist there (memory note
 * `headless-widgets-have-no-children-at-all`). The public functions below are what a click does, and a test drives
 * them.
 *
 * WHAT IT DOES NOT DO: show a skill's numbers (ruled Q7: no numbers in this layer), change the skill bar (ruled Q6;
 * issue #1193 stays open), or reach a server. There is no server call, so a networked client changes only its own
 * copy.
 */
UCLASS()
class CATACLYSM_API UCataclysmSkillAssignmentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget

	/** The line above the six keys (ruled Q8): the basic attack has no choice. */
	static const TCHAR* BasicAttackLine;

	/** The screen's own refusal, for a skill clicked with no slot selected (ruled Q5). */
	static const TCHAR* NoSlotSelectedRefusal;

	/** What the clear control says, in the owner's words. */
	static const TCHAR* ClearLabelText;

	/**
	 * Read the player again and redraw every key, the pool and the clear control. On every opening, after every
	 * choice, and when the worn weapons change. Safe with no player: every key reads empty.
	 */
	void Refresh();

	/**
	 * Redraw when the worn weapons changed since the last redraw (ruled Q9), by the equipment's `ChangeCount`.
	 * `NativeTick` calls it; public so a test can.
	 *
	 * @return whether it redrew
	 */
	bool RefreshIfGearChanged();

	/**
	 * What a click on a key's button does: select it, for the next skill clicked.
	 *
	 * @return false, selecting nothing, for a key that is not one of the six
	 */
	bool SelectSlot(ECataclysmAbilitySlot Key);

	/**
	 * What a click on a pool skill's button does: put it on the selected key, through
	 * `ACataclysmPlayerState::AssignSkill`, or show why not.
	 *
	 * @return whether the choice was made
	 */
	bool PickSkill(FName SkillRow);

	/**
	 * "Clear the slot" (the owner's Q5 and O5): forget the selected key's choice, so it returns to its default.
	 *
	 * @return whether there was a choice to forget
	 */
	bool ClearSelectedSlot();

	/** Whether the selected key has a choice to clear, which is when the clear control is enabled. */
	bool CanClearSelectedSlot() const;

	/** The key selected, or None. */
	ECataclysmAbilitySlot GetSelectedSlot() const { return SelectedSlot; }

	/** The six keys as the last redraw read them, in the skill bar's order (ruled Q8). */
	const TArray<FCataclysmSkillScreenRow>& SlotRows() const { return ShownRows; }

	/** The pool as the last redraw read it: one button each. */
	const TArray<FCataclysmWeaponSkill>& PoolRows() const { return ShownPool; }

	/**
	 * A skill's tool tip, four lines and no numbers (ruled Q7): its name; the slot it was made for, and the key it sits
	 * on when that is another; its weapon type and damage type; its description.
	 *
	 * @param OnKey  the key the skill sits on now, or None
	 */
	static FString ToolTipFor(const FCataclysmWeaponSkill& Skill, ECataclysmAbilitySlot OnKey);

	/**
	 * The line under a key whose choice is kept while its weapon is off (ruled Q4): "Kept: Pyroclasm (Greataxe, a
	 * middle dot, Demonic). It returns when that weapon is worn." Read from the weapon skill table, because a kept
	 * skill is not in the pool.
	 */
	static FString KeptTextFor(FName SkillRow);

	/** What the status line, the refusal line and the description line say. Kept here, so a test can read them. */
	const FString& StatusText() const { return LastStatus; }
	const FString& RefusalText() const { return LastRefusal; }
	const FString& DescriptionText() const { return LastDescription; }

	// ----------------------------------------------------------------------
	// Test seams
	// ----------------------------------------------------------------------

	/** The player state to show when there is no owning player to ask: a headless test has none. */
	void SetPlayerStateForTests(ACataclysmPlayerState* State);

	/**
	 * Describe `Button` as this screen describes a key's button (`bIsSlot`, `Value` the key's name, "Heavy") or a pool
	 * skill's (`Value` its row), for the player shown. The same function the panels run, on a button the test made.
	 */
	void DescribeButtonForTests(UCataclysmChoiceButton& Button, FName Value, bool bIsSlot);

protected:
	/** Which key is selected, or what to do first. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusLabel;

	/**
	 * The basic attack's line and the six keys, one button each, and a kept choice's line under its key.
	 *
	 * A PANEL AND NOT SIX NAMED SLOTS, for the reason the character sheet holds a panel and not eight buttons: which
	 * keys there are is `UCataclysmSkillBar::SlotsShown`, not this layout.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> SlotBox;

	/** One button per skill in the pool. A wrap box in the Blueprint: a pool of two weapons is longer than a line. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> PoolBox;

	/** "Clear the slot". Enabled only while the selected key has a choice. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ClearButton;

	/** The words on the clear control. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ClearLabel;

	/** The hovered skill's tool tip text, for a player who reads it in place rather than in the pop-up. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DescriptionLabel;

	/** Why the last click did nothing, when it did nothing. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	/** The screen's heading. Optional, because a heading is decoration. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/** What one key or skill button is drawn as. Soft, for the reason the character sheet's is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Skills")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Skills")
	FText Title = NSLOCTEXT("Cataclysm", "SkillAssignmentTitle", "Skills");

	/**
	 * How the lines this screen makes at run time are drawn: the basic attack's line and a kept choice's. Properties
	 * for the reason `UCataclysmCharacterSheetWidget::RowFontSize` gives: a text block made in C++ carries the engine's
	 * defaults. The kept line is dimmed (ruled Q4).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Skills")
	int32 LineFontSize = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Skills")
	FSlateColor LineColour = FSlateColor(FLinearColor(0.961f, 0.941f, 0.918f, 1.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Skills")
	FSlateColor KeptLineColour = FSlateColor(FLinearColor(0.44f, 0.47f, 0.51f, 1.0f));

private:
	/** The player state: the owning player's, else the test's. */
	ACataclysmPlayerState* ShownState() const;

	/** Its possessed character, or null. */
	ACataclysmPlayerCharacter* ShownCharacter() const;

	/** Its weapon slots, which hold the pool and what is on each key, or null. */
	const UCataclysmWeaponSlotsComponent* ShownWeaponSlots() const;

	/** Read the six keys and the pool into `ShownRows` and `ShownPool`. */
	void ReadModel();

	/** Rebuild the buttons in the two panels, when they exist. */
	void RedrawPanels();

	/** Write the status line, the refusal line and the clear control. */
	void WriteLabels();

	/** One run-time line into `SlotBox`. */
	void AddLine(const FString& Text, const FSlateColor& Colour);

	/** A key's button, as `DescribeButtonForTests` describes it. */
	void DescribeSlotButton(UCataclysmChoiceButton& Button, const FCataclysmSkillScreenRow& Row) const;

	/** A pool skill's button, as `DescribeButtonForTests` describes it. */
	void DescribePoolButton(UCataclysmChoiceButton& Button, const FCataclysmWeaponSkill& Skill) const;

	/** The key a skill row sits on now, or None. */
	ECataclysmAbilitySlot KeyHolding(FName SkillRow) const;

	/** The key a key button's value names, or None. */
	static ECataclysmAbilitySlot KeyNamed(FName Value);

	UFUNCTION()
	void HandleSlotClicked(FName Value);

	UFUNCTION()
	void HandleSkillClicked(FName Value);

	UFUNCTION()
	void HandleSkillHovered(FName Value);

	UFUNCTION()
	void HandleClearClicked();

	/** See `GetSelectedSlot`. */
	ECataclysmAbilitySlot SelectedSlot = ECataclysmAbilitySlot::None;

	/** See `SlotRows` and `PoolRows`. */
	TArray<FCataclysmSkillScreenRow> ShownRows;
	TArray<FCataclysmWeaponSkill> ShownPool;

	/** See `StatusText`, `RefusalText` and `DescriptionText`. */
	FString LastStatus;
	FString LastRefusal;
	FString LastDescription;

	/** The equipment's `ChangeCount` at the last redraw; -1 before the first. */
	int32 LastChangeCount = -1;

	/** The buttons in the two panels. Empty in a headless test. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> SlotButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> PoolButtons;

	/** Whether the clear control's click is bound. Bound once. */
	bool bClearBound = false;

	/** See `SetPlayerStateForTests`. */
	UPROPERTY(Transient)
	TObjectPtr<ACataclysmPlayerState> PlayerStateForTests;
};
