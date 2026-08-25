// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CataclysmChoiceButton.generated.h"

class UButton;
class UTextBlock;

/** Which option was clicked, by the name the screen knows it as. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCataclysmChoiceClicked,
										   FName, Value);

/**
 * One option in a list the player picks from: a weapon type, a damage type.
 *
 * WHY THE SCREEN DOES NOT PLACE THESE BY HAND. There are fourteen weapon types
 * and eight damage types, and both lists come out of a data table. A layout
 * holding twenty-two buttons drawn one at a time would have to be edited every
 * time the design workbook gained a weapon, and a designer would have no way to
 * know which button was which. So the screen holds two empty panels and fills
 * them with copies of this, which is the ordinary UMG arrangement for a list
 * whose length is not known when the layout is drawn.
 *
 * THE THREE STATES IT HAS TO SHOW, and they are not the same question:
 *
 *   available and not chosen   an option the player could take
 *   available and chosen       the one they have taken
 *   not available              a real option that this choice rules out --
 *                              War damage on a Staff, for instance, which the
 *                              design excludes because War has no caster build
 *
 * A NOT-AVAILABLE OPTION IS SHOWN AND DISABLED RATHER THAN HIDDEN. A list that
 * changed length as the player clicked around would move every button under
 * the cursor, and the player would never learn which weapons carry which damage
 * types, which is the one thing this screen is teaching.
 */
UCLASS()
class CATACLYSM_API UCataclysmChoiceButton : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Clicked, carrying the value this button stands for. */
	UPROPERTY(BlueprintAssignable, Category = "Cataclysm|Creation")
	FCataclysmChoiceClicked OnChoiceClicked;

	/**
	 * What this button stands for and how it should look.
	 *
	 * SAFE TO CALL BEFORE THE WIDGET IS CONSTRUCTED, and it has to be: the
	 * screen creates these and sets them up in the same breath. Everything it
	 * writes is kept, and `NativeConstruct` writes it into the widgets when they
	 * arrive.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	void SetChoice(FName InValue, const FText& InLabel, bool bInChosen,
				   bool bInAvailable);

	/** The value this button stands for. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FName GetChoiceValue() const { return Value; }

	/** Whether this is the option currently taken. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	bool IsChosen() const { return bChosen; }

	/** Whether this option can be taken at all. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	bool IsAvailable() const { return bAvailable; }

	/**
	 * What the click handler does, separated from the click.
	 *
	 * PUBLIC SO A TEST CAN PRESS IT. The automation command passes `-nullrhi`,
	 * so no test can produce a real mouse click on a real button; calling this
	 * is the nearest thing to pressing it, and it is the whole of what pressing
	 * it does.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	void Press();

	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

protected:
	/**
	 * The button itself.
	 *
	 * `BindWidget` RATHER THAN `BindWidgetOptional`, so a Widget Blueprint
	 * without one refuses to compile. A choice button with no button in it is
	 * not a partial layout, it is a button that cannot be clicked.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ChoiceButton;

	/** The words on it. Required for the same reason. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ChoiceLabel;

	/**
	 * The colour of an option that has been taken, and of one that has not.
	 *
	 * HERE RATHER THAN IN THE BLUEPRINT BECAUSE THE STATE IS DECIDED HERE. Which
	 * of the two a particular button gets is a judgement about the game, and
	 * `docs/DECISIONS.md` puts those in C++. What the two colours actually ARE
	 * is a look, which is why they are `EditAnywhere`: a designer opening
	 * `WBP_ChoiceButton` sets them and this file never has to change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Creation")
	FLinearColor ChosenColour = FLinearColor(1.0f, 0.85f, 0.45f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Creation")
	FLinearColor PlainColour = FLinearColor(0.96f, 0.94f, 0.92f, 1.0f);

	/**
	 * The colour of an option this choice rules out.
	 *
	 * DIMMED AND STILL READABLE, which is the point of the value chosen. A
	 * player has to be able to read which damage types a Staff cannot carry --
	 * that is the information the disabled state carries and the only reason
	 * the button is still drawn rather than removed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Creation")
	FLinearColor UnavailableColour = FLinearColor(0.44f, 0.47f, 0.51f, 1.0f);

	/** Writes the current state into the two bound widgets, if they exist. */
	void RefreshDisplay();

private:
	/** Bound to the button's click. Forwards to `Press`. */
	UFUNCTION()
	void HandleClicked();

	FName Value;
	FText Label;
	bool bChosen = false;
	bool bAvailable = true;
};
