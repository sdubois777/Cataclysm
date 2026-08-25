// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Character/CataclysmCharacterCreation.h"
#include "CataclysmCharacterCreationWidget.generated.h"

class UButton;
class UCataclysmChoiceButton;
class UDataTable;
class UPanelWidget;
class UTextBlock;

/**
 * The character creator: choose a starting weapon type and a damage type.
 *
 * `docs/Cataclysm_GDD_v2.md` section IV is what this is. Part of issue #50.
 *
 * THE FIRST SCREEN BUILT THE WAY `docs/DECISIONS.md` DECIDED ON 2026-08-24. The
 * logic and the state are here, in C++, where a diff shows them and an
 * automation test can reach them. The layout is `WBP_CharacterCreation` under
 * `game/Content/Interface/`, which derives from this class and is edited in
 * Unreal's designer. `UPROPERTY(meta = (BindWidget))` is the join, and the
 * Blueprint compiler refuses to compile a tree that is missing one of them, so
 * the two halves cannot quietly drift apart.
 *
 * WHY EVERY FUNCTION HERE WORKS WITH NO WIDGETS AT ALL. The automation command
 * in `tools/unreal_build.py` passes `-nullrhi` and runs with no editor, so a
 * test can construct this class but cannot give it a Blueprint, and every bound
 * pointer below is therefore null in a test. Each is checked before it is
 * touched. That is not defensive habit: it is what makes the choosing, the
 * refusing and the confirming coverable at all.
 *
 * WHAT IT DOES NOT OFFER. The four appearance choices the design also
 * describes -- preset body types, skin tones, hairstyles and height. The project
 * owner asked on 2026-08-24 to leave them out, because this project has no
 * player character art for them to change. Issue #931.
 *
 * WHAT CONFIRMING DOES NOT DO. It does not change which class stat line the
 * character sits on. A character created as War still stands on the Ravager
 * line, because twenty-one of the twenty-four classes have no stat line at all
 * and nothing decides which of a damage type's three a character gets. Issue
 * #932 carries that, and it cannot be settled here.
 */
UCLASS()
class CATACLYSM_API UCataclysmCharacterCreationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ The state of the choice.

	/** What has been chosen so far. Either half may be empty. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FCataclysmCreationChoice GetChoice() const { return Choice; }

	/**
	 * Take this weapon type.
	 *
	 * A DAMAGE TYPE THE NEW WEAPON CANNOT CARRY IS DROPPED, not kept and
	 * refused at the end. A player who picks Staff, then Void, then changes to
	 * Axe would otherwise be looking at a screen showing Void chosen, Void
	 * greyed out, and a refusal at the bottom -- three statements of one fact,
	 * two of which contradict the third.
	 *
	 * @return false when the weapon type is not one the creator offers
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	bool ChooseWeaponType(FName WeaponType);

	/**
	 * Take this damage type.
	 *
	 * @return false when no weapon type is chosen yet, or the chosen one cannot
	 *         carry this damage type
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	bool ChooseDamageType(FName DamageType);

	//~ What the screen offers.

	/** Every weapon type a character may start with, in the order shown. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	TArray<FName> OfferedWeaponTypes() const;

	/**
	 * All eight damage types, in the design's order, whatever is chosen.
	 *
	 * ALL EIGHT ALWAYS, and which of them can be taken is a separate question
	 * that `DamageTypeIsAvailable` answers. A list that shrank as the player
	 * clicked would move every button under the cursor and would never teach
	 * them which weapons carry which types.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	TArray<FName> OfferedDamageTypes() const;

	/** Whether the chosen weapon type can carry this damage type. False for
	 *  every damage type while no weapon type is chosen. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	bool DamageTypeIsAvailable(FName DamageType) const;

	//~ What the screen says.

	/** The chosen pair and how many of its six skills are written. Empty until
	 *  both halves are chosen. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FText SummaryText() const;

	/** The three class trees the chosen damage type unlocks. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FText UnlockedClassesText() const;

	/** Why the choice cannot be confirmed, or empty when it can be. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FText RefusalText() const;

	/** Whether confirming would do anything. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	bool CanConfirm() const;

	/**
	 * Take the choice: record it, and make the character match it.
	 *
	 * WHAT IT ACTUALLY CHANGES. The player state remembers the pair, which is
	 * what the save record is written from and what survives death. The pawn
	 * then puts on a weapon of the chosen type and its six ability slots are
	 * filled from the chosen damage type, so the change is visible in the gear
	 * panel and on the skill bar immediately rather than at the next respawn.
	 *
	 * @return false and changes nothing when `CanConfirm` is false, or when
	 *         there is no player state to record it on
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	bool Confirm();

	/**
	 * Use these tables rather than the generated ones.
	 *
	 * For tests, which build tables they control so that a pairing the real
	 * matrix does not contain can be set up. Passing null for either restores
	 * the ordinary lazy load.
	 */
	void SetTables(const UDataTable* InWeaponSkillTable,
				   const UDataTable* InBaseTable);

	/** Rewrite every bound widget from the current state. Does nothing useful
	 *  with no Blueprint, which is the case in every automation test. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	void RefreshDisplay();

	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

protected:
	/** Where the weapon type buttons go. The designer places the panel; this
	 *  class fills it. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> WeaponTypeBox;

	/** Where the damage type buttons go. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> DamageTypeBox;

	/** The chosen pair and its designed skill count. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SummaryLabel;

	/** The three class trees the damage type unlocks. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> UnlockedLabel;

	/** Why the choice cannot be taken yet. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	/** Takes the choice and closes the screen. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ConfirmButton;

	/**
	 * The screen's heading.
	 *
	 * `BindWidgetOptional` AND IT IS THE ONLY ONE, because a heading is
	 * decoration: a layout that says what it is some other way, or says it in
	 * the frame around the screen, is a legitimate layout. Every other bound
	 * widget above carries something the player cannot get anywhere else.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/**
	 * What one option in either list is drawn as.
	 *
	 * A SOFT PATH WITH A DEFAULT, AND STILL `EditAnywhere` SO A DESIGNER OWNS
	 * IT. The screen makes one of these per weapon type and per damage type,
	 * and what one looks like is layout, so the answer belongs in a Widget
	 * Blueprint. The path is the one
	 * `tools/generate_interface_assets.py` writes.
	 *
	 * SOFT RATHER THAN HARD for the reason
	 * `ACataclysmPlayerController::CharacterCreationScreenClass` gives: a
	 * checkout that has not run the generator has no such asset, and a hard
	 * reference to a missing class complains on every start of the editor about
	 * a screen nobody has opened.
	 *
	 * NOTHING IS DRAWN WHEN IT CANNOT BE LOADED, and the screen says so in the
	 * log rather than silently showing two empty panels.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Creation")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

	/** The heading, when the layout has one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Creation")
	FText Title = NSLOCTEXT("Cataclysm", "CreationTitle", "Create a character");

private:
	/** Bound to the confirm button. */
	UFUNCTION()
	void HandleConfirmClicked();

	/** Bound to every weapon type button. */
	UFUNCTION()
	void HandleWeaponTypeClicked(FName WeaponType);

	/** Bound to every damage type button. */
	UFUNCTION()
	void HandleDamageTypeClicked(FName DamageType);

	/** The weapon skill matrix, loaded once when nothing has set one. */
	const UDataTable* WeaponSkills() const;

	/** The item bases table, the same way. */
	const UDataTable* ItemBases() const;

	/** Empty a panel and refill it with one choice button per value. */
	void FillChoicePanel(UPanelWidget* Panel, const TArray<FName>& Values,
						 bool bWeaponTypes);

	/**
	 * `ChoiceButtonClass` loaded, or null with one complaint in the log.
	 *
	 * LOADED ONCE AND KEPT, because `FillChoicePanel` runs on every click and
	 * resolving a soft path each time would be a lookup for something that
	 * cannot change while the screen is open.
	 */
	UClass* LoadedChoiceButtonClass();

	UPROPERTY(Transient)
	TSubclassOf<UCataclysmChoiceButton> ResolvedChoiceButtonClass;

	/** So the complaint above is made once rather than on every click. */
	bool bComplainedAboutTheChoiceButtonClass = false;

	UPROPERTY()
	FCataclysmCreationChoice Choice;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> WeaponSkillTable;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> BaseTable;
};
