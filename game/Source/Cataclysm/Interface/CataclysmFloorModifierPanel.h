// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Interface/CataclysmFloorModifierPanelLayout.h"
#include "CataclysmFloorModifierPanel.generated.h"

class UPanelWidget;
class UTextBlock;

/**
 * The dungeon modifiers in force on the floor being walked, shown in a corner of
 * the screen while the player walks it. Issue #41.
 *
 * WHY IT EXISTS. No screen named a dungeon's modifiers before this. The only
 * place they appeared was a count and a danger total in `Cataclysm.ShowEmpire`,
 * so a player on a floor could not tell what it carried or whether any of it did
 * anything. See `UCataclysmFloorModifierPanelLayout` for what each line says.
 *
 * THE LAYOUT IS IN A WIDGET BLUEPRINT AND THE LOGIC IS HERE, the arrangement
 * every screen in this project uses since `docs/DECISIONS.md`'s entry of
 * 2026-08-24. `tools/generate_interface_assets.py` writes the first version of
 * `WBP_FloorModifiers` and refuses to touch it afterwards.
 *
 * IT NEVER TAKES A CLICK. It sits over the game while the player plays, and a
 * panel that caught the mouse would stop click-to-move working wherever it is
 * drawn. `NativeConstruct` makes it invisible to hit tests.
 *
 * SHOWN BY `ACataclysmPlayerController::ShowFloorModifiers`, which the dungeon
 * game mode calls whenever a floor begins. A floor with no modifiers hides it.
 */
UCLASS()
class CATACLYSM_API UCataclysmFloorModifierPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

	/**
	 * Which modifiers to list, and which floor they are on. Redraws.
	 *
	 * @param RowKeys     `FCataclysmFloorBrief::Modifiers`
	 * @param FloorNumber counted from 1
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	void SetFloorModifiers(const TArray<FName>& RowKeys, int32 FloorNumber);

	/** What the panel is showing, for a test that cannot read the screen. */
	const TArray<FCataclysmFloorModifierLine>& ShownLines() const { return Lines; }

	/** Which floor the panel is showing. */
	int32 ShownFloorNumber() const { return FloorNumber; }

protected:
	/** "Floor 5: 2 dungeon modifiers". */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HeadingLabel;

	/**
	 * Where one name line and one description line per modifier go.
	 *
	 * A SCROLL BOX IN THE BLUEPRINT, because a Sacrificial dungeon at difficulty
	 * tier 8 carries sixteen modifiers and their descriptions do not fit a
	 * corner of the screen.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ModifierBox;

	/**
	 * How the rows are drawn.
	 *
	 * PROPERTIES RATHER THAN A STYLE IN THE BLUEPRINT, for the reason the city
	 * screen's are: the rows are made at run time and a text block made in C++
	 * carries the engine's defaults rather than anything a designer set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Dungeon")
	int32 NameFontSize = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Dungeon")
	int32 DescriptionFontSize = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Dungeon")
	FSlateColor RowColour = FSlateColor(FLinearColor(0.961f, 0.941f, 0.918f, 1.0f));

	/**
	 * What a modifier that does nothing yet is drawn in.
	 *
	 * DIMMER THAN A BUILT ONE, so a player reads it as unfinished work rather
	 * than as a rule they are failing to notice in play.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Dungeon")
	FSlateColor DimColour = FSlateColor(FLinearColor(0.545f, 0.529f, 0.514f, 1.0f));

private:
	/** Writes the heading and the rows from `Lines`. */
	void Redraw();

	/** One row into `ModifierBox`, as a wrapping text block. */
	void AddRow(const FString& Text, int32 FontSize, bool bDim);

	/** What is being shown. */
	TArray<FCataclysmFloorModifierLine> Lines;

	/** Which floor it is on. */
	int32 FloorNumber = 1;

	/** One text block per row, name and description lines alike. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Rows;
};
