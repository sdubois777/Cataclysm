// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "CataclysmCityScreenWidget.generated.h"

class UCataclysmChoiceButton;
class UCataclysmEmpireRun;
class UPanelWidget;
class UTextBlock;

/**
 * One city: what it is worth, what is standing on it, and what it can build.
 *
 * WHY IT EXISTS. Clicking a city on the empire overview wrote "There is nothing
 * to do at a city yet" into a text line, and buying a city upgrade needed a
 * console command. `game/Data/CityUpgrades.csv` holds 24 upgrades and ten of
 * them work; nothing in the game let a player spend one. Issue #42.
 *
 * THE LAYOUT IS IN A WIDGET BLUEPRINT AND THE LOGIC IS HERE, which is what
 * `docs/DECISIONS.md` decided on 2026-08-24 and how every other screen in this
 * project is built. `tools/generate_interface_assets.py` writes the first
 * version of `WBP_CityScreen` and refuses to touch it afterwards, so a designer
 * owns how it looks and this file owns what it says.
 *
 * WHERE THE WORDS ARE DECIDED. `UCataclysmCityScreenLayout`, for the reason
 * `UCataclysmEmpireMapLayout` and `UCataclysmCharacterSheetLayout` exist: the
 * automation test command passes `-nullrhi` and draws nothing, so anything that
 * reaches the screen cannot be watched by a test. What each line says is
 * covered; **whether the result is legible is not, and no test on this project
 * can tell anyone that.**
 *
 * IT SHOWS THE FOURTEEN UPGRADES THAT DO NOTHING YET, in a section of their own
 * with the reason beside each. Hiding them would make the game look as though it
 * has ten city upgrades rather than twenty-four with fourteen waiting on systems
 * that do not exist.
 *
 * WHAT IT DOES NOT DO YET:
 *
 *   - **It cannot sell an upgrade back.** Nothing in the design says a slot can
 *     be freed, and a one-time upgrade has already fired, so there is nothing to
 *     undo.
 *   - **It shows no price and no build time.** A city upgrade is free and
 *     immediate because neither number is designed and there is no gold. Issue
 *     #1264.
 *   - **It does not let a player enter a dungeon standing on the city.**
 *     `Cataclysm.EnterDungeon` does that and only works from inside a dungeon
 *     level, because there is no travel between the empire and a dungeon. Issue
 *     #48.
 *   - **It has no key of its own**, for the reason the empire overview and the
 *     character sheet have none: the input assets are generated in the editor
 *     and adding a binding is a separate change from building a screen.
 */
UCLASS()
class CATACLYSM_API UCataclysmCityScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

	/**
	 * Which city to show. Redraws.
	 *
	 * ACCEPTS A CITY THAT DOES NOT EXIST, and says so on the screen rather than
	 * refusing, because the caller is a click on a map and a screen that stays
	 * blank reads as broken.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void SetCity(int32 CityId);

	/** Which city is being shown. `INDEX_NONE` before `SetCity` runs. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 ShownCityId() const { return CityId; }

	/**
	 * Reads the run again and redraws everything.
	 *
	 * SAFE TO CALL WHEN THERE IS NO RUN. Every label says so and no button is
	 * made, which is what a screen opened before a run started should show.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void Refresh();

	/** The run being shown, or null. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	UCataclysmEmpireRun* ShownRun() const;

	// ----------------------------------------------------------------------
	// Test seams
	// ----------------------------------------------------------------------

	/**
	 * The run to show when there is no game instance to ask.
	 *
	 * A HEADLESS TEST HAS NO GAME INSTANCE OF THIS CLASS, so without this seam
	 * the screen would have nothing to draw and every test of it would be a test
	 * of the empty case. `UCataclysmEmpireMapWidget::SetRunForTests` is the same
	 * seam for the same reason.
	 */
	void SetRunForTests(UCataclysmEmpireRun* Run);

	/** What each label says, for a test that cannot read the screen. */
	FString TitleText() const;
	FString StatusText() const;
	FString SlotsText() const;
	FString RefusalText() const;

	/** How many upgrades this city is being offered as a button. */
	int32 OfferCount() const { return OfferButtons.Num(); }

	/** The row name one offer button stands for, or `NAME_None`. */
	FName OfferRowName(int32 Index) const;

	/** How many rows are in the detail list: dungeons, what the city has, and
	 *  the upgrades that do nothing yet. */
	int32 DetailRowCount() const { return DetailRows.Num(); }

	/** What one detail row says, or empty when there is no such row. */
	FString DetailRowText(int32 Index) const;

	/**
	 * Buys an upgrade as though its button had been clicked.
	 *
	 * A TEST CANNOT CLICK. `-nullrhi` draws nothing and a button that is never
	 * drawn is never pressed, so the handler is reachable by name. It is the
	 * same handler the button calls, not a copy of it.
	 */
	void ClickOfferForTests(FName RowName);

protected:
	/** Which city this is, and what tier. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleLabel;

	/** Defence, population, and whether the Cataclysm can reach it. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusLabel;

	/** How many upgrade slots are filled, of how many. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SlotsLabel;

	/** Why the last click did nothing, when it did nothing. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	/**
	 * Where one button per buyable upgrade goes.
	 *
	 * A PANEL AND NOT A FIXED NUMBER OF SLOTS, for the reason the character
	 * sheet holds a panel rather than eight named buttons: how many a city can
	 * buy changes with what it already has and with how many effects are built.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> OfferBox;

	/**
	 * Where the dungeon lines, the upgrades held, and the upgrades that do
	 * nothing yet go.
	 *
	 * A SCROLL BOX IN THE BLUEPRINT, because 14 unbuilt upgrades plus what the
	 * city holds plus its dungeons do not fit a window.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> DetailBox;

	/** What one upgrade button is drawn as. Soft, for the reason the empire
	 *  overview's is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Empire")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

	/**
	 * How the rows this screen builds are drawn.
	 *
	 * FOUR PROPERTIES RATHER THAN A STYLE IN THE BLUEPRINT, and it is not a
	 * shortcut, for the reason the character sheet's are: the rows are made at
	 * run time, and a text block made in C++ carries the ENGINE's defaults --
	 * 24 point black -- rather than anything the designer set, because there is
	 * no placed widget for them to inherit from. Exposing them here is what puts
	 * the look back in the Blueprint's hands.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Empire")
	int32 RowFontSize = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Empire")
	int32 HeadingFontSize = 22;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Empire")
	FSlateColor RowColour =
		FSlateColor(FLinearColor(0.961f, 0.941f, 0.918f, 1.0f));

	/**
	 * What an upgrade that does nothing yet is drawn in.
	 *
	 * DIMMER THAN A LIVE ROW, so the fourteen that cannot be bought read as
	 * unfinished work rather than as choices a player is failing to notice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Empire")
	FSlateColor DimColour =
		FSlateColor(FLinearColor(0.545f, 0.529f, 0.514f, 1.0f));

private:
	/** Writes the three labels. */
	void WriteStatus();

	/** Makes one button per upgrade this city could buy, replacing whatever was
	 *  there. */
	void BuildOfferButtons();

	/** Writes the dungeon lines, what the city holds, and what does nothing
	 *  yet, replacing whatever was there. */
	void WriteDetailRows();

	/** One row into `DetailBox`, as a text block. */
	void AddRow(const FString& Text, bool bIsHeading, bool bDim);

	UFUNCTION()
	void HandleOfferClicked(FName Value);

	/** Which city is being shown. */
	int32 CityId = INDEX_NONE;

	/** Why the last click did nothing. Empty when it did something. */
	FString Refusal;

	/** One button per buyable upgrade, and the row each stands for. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> OfferButtons;

	TArray<FName> OfferRowNames;

	/** One text block per detail row, headings included. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> DetailRows;

	/** The run supplied by a test. See `SetRunForTests`. */
	UPROPERTY(Transient)
	TObjectPtr<UCataclysmEmpireRun> RunForTests;
};
