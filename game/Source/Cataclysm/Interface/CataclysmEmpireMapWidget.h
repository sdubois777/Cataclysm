// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "CataclysmEmpireMapWidget.generated.h"

class UCanvasPanel;
class UCataclysmChoiceButton;
class UCataclysmEmpireRun;
class UTextBlock;

/**
 * The empire overview: all 25 cities, what is standing on them, and how long
 * until the next wave.
 *
 * WHAT THE DESIGN ASKS FOR, `docs/Cataclysm_GDD_v2.md` section XIII: "Main
 * Empire Overview -- shows all cities, active dungeons, next surge timer, empire
 * status."
 *
 * THE LAYOUT IS IN A WIDGET BLUEPRINT AND THE LOGIC IS HERE, which is what
 * `docs/DECISIONS.md` decided on 2026-08-24 and how every other screen in this
 * project is built. `tools/generate_interface_assets.py` writes the first
 * version of `WBP_EmpireMap` and refuses to touch it afterwards, so a designer
 * owns how it looks and this file owns what it says.
 *
 * WHERE THE ARITHMETIC IS. `UCataclysmEmpireMapLayout`, for the reason
 * `UCataclysmPassiveTreeLayout` exists: the automation test command passes
 * `-nullrhi` and draws nothing, so anything that reaches the screen cannot be
 * watched by a test. Where a city lands and what its box says are covered;
 * whether the result is legible is not, and **no test on this project can tell
 * anyone that**.
 *
 * CLICKING A CITY OPENS ITS SCREEN. `UCataclysmCityScreenWidget` shows what the
 * city is worth, what is standing on it and what it can build, and a player
 * spends its upgrade slots there. This asks the player controller to open it,
 * because the controller owns every screen in this project. Issue #42.
 *
 * WHAT IT DOES NOT DO YET:
 *
 *   - **A city cannot be entered from here.** Clicking one opens its screen; no
 *     dungeon standing on it can be walked from either, because there is no
 *     travel between the empire and a dungeon level. Issue #48.
 *   - **It does not advance the day.** Nothing in the game does except a console
 *     command. Walking a dungeon and spending days at the forge are what
 *     eventually will.
 *   - **It draws no lanes between cities.** The diamond's adjacency is visible
 *     from the arrangement, and a line per lane would be 40 more widgets saying
 *     what the grid already says.
 */
UCLASS()
class CATACLYSM_API UCataclysmEmpireMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;
	//~ End UUserWidget

	/**
	 * Reads the run again and redraws everything.
	 *
	 * SAFE TO CALL WHEN THERE IS NO RUN. The map is emptied and the labels say
	 * so, which is what a screen opened before a run started should show.
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
	 * of the empty case. `UCataclysmPassiveTreeWidget::StateForTests` is the
	 * same seam for the same reason.
	 */
	void SetRunForTests(UCataclysmEmpireRun* Run);

	/**
	 * The panel size to lay the diamond out against.
	 *
	 * A HEADLESS TEST HAS NO GEOMETRY AT ALL, so the panel would answer the same
	 * stated guess for ever and a test could not make it change size. Zero means
	 * nothing was said. Issue #1078 recorded this for the passive tree.
	 */
	void SetPanelSizeForTests(FVector2D Size);

	/** How many city boxes are on the map. Twenty-five once a run is shown. */
	int32 CityCount() const { return CityButtons.Num(); }

	/** Where a city's box was placed, or the zero vector if it has none. */
	FVector2D PlacedAt(int32 CityId) const;

	/** The day the drawing was made for. */
	int32 ShownDay() const { return DrawnForDay; }

	/** What the three labels say, for a test that cannot read the screen. */
	FString StatusText() const;
	FString SurgeText() const;
	FString DetailText() const;

	/** Which city was clicked last, or `INDEX_NONE` before any was. */
	int32 LastClickedCityId() const { return ClickedCityId; }

	/**
	 * Clicks a city's box as though a player had.
	 *
	 * A TEST CANNOT CLICK. `-nullrhi` draws nothing and a box that is never
	 * drawn is never pressed, so the handler is reachable by city. It is the
	 * same handler the button calls, not a copy of it.
	 */
	void ClickCityForTests(int32 CityId);

protected:
	/**
	 * Where the cities are drawn: one box per city, placed by lattice
	 * coordinate.
	 *
	 * A CANVAS PANEL AND NOT A GRID, although the lattice is on a grid. Only 25
	 * of a 7 by 7 grid's 49 cells exist, so a grid would need 24 empty widgets
	 * to hold the diamond's shape, and the diamond would be locked to whatever
	 * spacing the Blueprint's grid was given rather than fitted to the panel.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> MapCanvas;

	/** The day, how many dungeons are standing, and how much has been lost. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusLabel;

	/** How long until the next wave and how big it will be. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SurgeLabel;

	/** What the city under the cursor is, and what is standing on it. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailLabel;

	/**
	 * The screen's heading. `BindWidgetOptional` because a heading is
	 * decoration, and it is the only optional one for that reason.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/** What one city is drawn as. See the passive tree's property of the same
	 *  shape for why this is a soft path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Empire")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Empire")
	FText Title = NSLOCTEXT("Cataclysm", "EmpireTitle", "The empire");

private:
	/** Makes one box per city. Called once, when the screen is built. */
	void BuildCities();

	/** Puts every box where it goes and writes what it says. */
	void PlaceCities();

	/** Writes the day, the surge countdown and the empire's state. */
	void WriteStatus();

	/** The panel's real size, or what a test said it is. */
	FVector2D CanvasSize() const;

	UFUNCTION()
	void HandleCityHovered(FName Value);

	UFUNCTION()
	void HandleCityClicked(FName Value);

	/** Which city a button stands for, or `INDEX_NONE`. */
	int32 CityForButton(FName Value) const;

	/**
	 * Which city was clicked last, or `INDEX_NONE` before any was.
	 *
	 * RECORDED BECAUSE OPENING THE SCREEN CANNOT BE WATCHED. A headless test has
	 * no player controller, so the part of a click that opens the city screen
	 * does nothing there. This is what lets a test check that clicking a
	 * particular box means a particular city, which is the half of the click
	 * that can go wrong silently.
	 */
	int32 ClickedCityId = INDEX_NONE;

	/** One box per city, in city identifier order. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> CityButtons;

	/** The run supplied by a test. See `SetRunForTests`. */
	UPROPERTY(Transient)
	TObjectPtr<UCataclysmEmpireRun> RunForTests;

	/** See `SetPanelSizeForTests`. Zero means nothing was said. */
	FVector2D PanelSizeForTests = FVector2D::ZeroVector;

	/**
	 * The day the drawing was made for, and the panel it was fitted to.
	 *
	 * WHAT THEY ARE FOR IS NOT REDRAWING EVERY FRAME. A tick that rebuilt 25
	 * boxes would be the interface cost issue #825 is about, and the empire only
	 * changes when a day passes or the panel is resized.
	 */
	int32 DrawnForDay = INDEX_NONE;
	FVector2D DrawnForSize = FVector2D::ZeroVector;
};
