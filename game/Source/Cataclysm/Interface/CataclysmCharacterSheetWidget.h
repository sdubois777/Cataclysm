// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "CataclysmCharacterSheetWidget.generated.h"

class UAbilitySystemComponent;
class UCataclysmChoiceButton;
class UPanelWidget;
class UTextBlock;

/**
 * What the character actually has: 46 stats, and the eight attributes a player
 * spends points into.
 *
 * WHY IT EXISTS. Nothing in this game showed a player a single stat. Not
 * health, not armour, not resistance, and not the difficulty tier's resistance
 * penalty, whose entire purpose is to tell a player they need more resistance.
 * `Cataclysm.ShowResistances` printed eight lines to the console and that was
 * the whole of it. Issue #1233.
 *
 * AND WHY IT SPENDS POINTS AS WELL AS SHOWING THEM. Until this screen,
 * `Cataclysm.SpendAttributePoint` was the only way to spend an attribute point
 * in this game. A player who levelled up could not act on it without opening
 * the console. Issue #50.
 *
 * THE LAYOUT IS IN A WIDGET BLUEPRINT AND THE LOGIC IS HERE, which is what
 * `docs/DECISIONS.md` decided on 2026-08-24 and how every screen in this project
 * is built. `tools/generate_interface_assets.py` writes the first version of
 * `WBP_CharacterSheet` and refuses to touch it afterwards, so a designer owns
 * how it looks and this file owns what it says.
 *
 * WHERE THE WORDS ARE DECIDED. `UCataclysmCharacterSheetLayout`, for the reason
 * `UCataclysmEmpireMapLayout` exists: the automation test command passes
 * `-nullrhi` and draws nothing, so anything that reaches the screen cannot be
 * watched by a test. Which stats appear, in which group, and what each row says
 * are all covered there; **whether the result is legible is not, and no test on
 * this project can tell anyone that.**
 *
 * WHAT IT DOES NOT DO YET:
 *
 *   - **It does not show Worn Residue, and the design says it should.**
 *     `docs/Cataclysm_GDD_v2.md` says Worn Residue "is shown on the character
 *     sheet at all times". It is not one of the 46 stats and correctly so -- it
 *     is a sum over equipped items rather than a stat with a baseline -- but
 *     nothing in the game computes that sum at all, so there is no figure to
 *     show. Issue #1251.
 *   - **Its armour penetration row reads zero for every character.** Nothing in
 *     `game/Data/` grants the stat; the three enchantments that would are
 *     blocked on enchantments existing. The row is shown anyway because the
 *     design lists it among the 46. Issue #1252.
 *   - **It does not show where a stat came from.** A player can read that they
 *     have 40% critical strike chance and not which piece of gear supplied it.
 *     `UCataclysmStatPipeline` knows; nothing asks it. Issue #1233 does not ask
 *     for this and it is the obvious next thing.
 *   - **It cannot take a point back.** Respeccing is the Trainer's job in the
 *     capital, which does not exist. `ACataclysmPlayerState::ResetAttributePoints`
 *     is what it will call. Issue #48.
 *   - **It has no key of its own.** `Cataclysm.CharacterSheet` opens it. The
 *     input assets are generated in the editor and adding a binding to them is a
 *     separate change from building a screen, which is the same reason the
 *     empire overview has no key.
 */
UCLASS()
class CATACLYSM_API UCataclysmCharacterSheetWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

	/**
	 * Reads the character again and rewrites every row.
	 *
	 * SAFE TO CALL WITH NO CHARACTER. Every figure reads zero and every row is
	 * still named, so a screen opened before a character exists shows the shape
	 * of the sheet rather than an empty panel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Character")
	void Refresh();

	/** The character being shown, or null. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Character")
	UAbilitySystemComponent* ShownAbilitySystem() const;

	// ----------------------------------------------------------------------
	// Test seams
	// ----------------------------------------------------------------------

	/**
	 * The character to show when there is no owning player to ask.
	 *
	 * A HEADLESS TEST HAS NO PLAYER CONTROLLER AND NO PAWN, so without this seam
	 * the screen would have nothing to read and every test of it would be a test
	 * of the empty case. `UCataclysmEmpireMapWidget::SetRunForTests` is the same
	 * seam for the same reason.
	 */
	void SetAbilitySystemForTests(UAbilitySystemComponent* ASC);

	/**
	 * The difficulty tier to work the armour and resistance rows out against.
	 *
	 * A HEADLESS TEST HAS NO WORLD WITH A GAME MODE IN IT, and the tier is asked
	 * of the world rather than stored on the character. Zero means nothing was
	 * said, and the tier the game reports is used.
	 */
	void SetDifficultyTierForTests(int32 Tier);

	/** The tier the rows were worked out against. */
	int32 ShownDifficultyTier() const;

	/** How many stat rows are on the screen. */
	int32 RowCount() const { return StatRows.Num(); }

	/** What one row says, or empty when there is no such row. */
	FString RowText(int32 Index) const;

	/** What the three labels say, for a test that cannot read the screen. */
	FString StatusText() const;
	FString PointsText() const;
	FString RefusalText() const;

protected:
	/** The heading: which class, and what level. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusLabel;

	/** How many attribute points are waiting to be spent. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PointsLabel;

	/** Why the last click did nothing, when it did nothing. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	/**
	 * Where the eight attributes go, one button each.
	 *
	 * A PANEL AND NOT EIGHT NAMED SLOTS, for the reason the character creator
	 * holds a panel rather than fourteen weapon buttons: the eight names come
	 * from `FCataclysmAttributePoints::Names`, and a Blueprint holding eight
	 * hand-placed buttons would have to be edited the day a ninth is added.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> AttributeBox;

	/**
	 * Where the 46 stat rows and the five headings go.
	 *
	 * A SCROLL BOX IN THE BLUEPRINT, because 51 lines do not fit a window. This
	 * is typed as the general panel so that whoever opens the designer may
	 * change it to a vertical box or a wrap box without breaking the bind.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> StatBox;

	/** The screen's heading. Optional, because a heading is decoration. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/** What one attribute button is drawn as. Soft, for the reason the empire
	 *  overview's is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Character")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Character")
	FText Title = NSLOCTEXT("Cataclysm", "CharacterSheetTitle", "Character");

	/**
	 * How the rows this screen builds are drawn.
	 *
	 * FOUR PROPERTIES RATHER THAN A STYLE IN THE BLUEPRINT, and it is not a
	 * shortcut. The 51 rows are made at run time, and a text block made in C++
	 * carries the ENGINE's defaults -- 24 point black -- rather than anything
	 * the designer set, because there is no placed widget for them to inherit
	 * from. Exposing them here is what puts the look back in the Blueprint's
	 * hands, which is the arrangement `docs/DECISIONS.md` chose on 2026-08-24.
	 *
	 * THE DEFAULTS ARE THE INVENTORY SCREEN'S INK, from
	 * `UCataclysmInventoryScreen::InkHex` by way of
	 * `tools/generate_interface_assets.py`, so an unedited sheet does not read
	 * as part of a different game.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Character")
	int32 RowFontSize = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Character")
	int32 HeadingFontSize = 22;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Character")
	FSlateColor RowColour =
		FSlateColor(FLinearColor(0.961f, 0.941f, 0.918f, 1.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Character")
	FSlateColor HeadingColour =
		FSlateColor(FLinearColor(0.961f, 0.941f, 0.918f, 1.0f));

private:
	/** Makes one button per attribute. Called once, when the screen is built. */
	void BuildAttributeButtons();

	/** Writes every stat row, replacing whatever was there. */
	void WriteStatRows();

	/** Writes the heading, the point count and each attribute button. */
	void WriteStatus();

	/** One row into `StatBox`, as a text block. */
	void AddRow(const FString& Text, bool bIsHeading);

	UFUNCTION()
	void HandleAttributeClicked(FName Value);

	/** The attribute a button stands for, or empty. */
	FString AttributeForButton(FName Value) const;

	/** The player state holding the points, or null. */
	class ACataclysmPlayerState* Points() const;

	/** One button per attribute, in the order `Names` gives them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> AttributeButtons;

	/** One text block per row, headings included. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> StatRows;

	/** The character supplied by a test. See `SetAbilitySystemForTests`. */
	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemForTests;

	/** See `SetDifficultyTierForTests`. Zero means nothing was said. */
	int32 DifficultyTierForTests = 0;
};
