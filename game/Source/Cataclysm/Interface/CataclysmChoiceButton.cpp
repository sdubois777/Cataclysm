// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmChoiceButton.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UCataclysmChoiceButton::SetChoice(FName InValue, const FText& InLabel,
									   bool bInChosen, bool bInAvailable)
{
	Value = InValue;
	Label = InLabel;
	bChosen = bInChosen;
	bAvailable = bInAvailable;
	RefreshDisplay();
}

void UCataclysmChoiceButton::Press()
{
	// A DISABLED BUTTON IS STILL PRESSED BY A TEST, and by anything else that
	// calls this directly, so the rule lives here rather than only in the
	// button's enabled state. Without it, a damage type a weapon cannot carry
	// could be chosen by any route that is not a mouse.
	if (!bAvailable)
	{
		return;
	}

	OnChoiceClicked.Broadcast(Value);
}

void UCataclysmChoiceButton::NativeConstruct()
{
	Super::NativeConstruct();

	if (ChoiceButton)
	{
		// CLEARED FIRST. NativeConstruct runs again every time the widget is
		// added to the viewport, and a delegate added twice fires twice, which
		// would choose an option and then choose it again.
		ChoiceButton->OnClicked.RemoveAll(this);
		ChoiceButton->OnClicked.AddDynamic(this,
										   &UCataclysmChoiceButton::HandleClicked);
	}

	// WHATEVER `SetChoice` WAS TOLD BEFORE THE WIDGETS EXISTED. The screen makes
	// these and describes them in one breath, which happens before Slate has
	// built anything, so without this the first thing a player sees is
	// twenty-two blank buttons.
	RefreshDisplay();
}

void UCataclysmChoiceButton::RefreshDisplay()
{
	if (ChoiceLabel)
	{
		ChoiceLabel->SetText(Label);
		ChoiceLabel->SetColorAndOpacity(FSlateColor(
			!bAvailable ? UnavailableColour
						: (bChosen ? ChosenColour : PlainColour)));
	}

	if (ChoiceButton)
	{
		ChoiceButton->SetIsEnabled(bAvailable);
	}
}

void UCataclysmChoiceButton::NativeOnMouseEnter(const FGeometry& Geometry,
												const FPointerEvent& Event)
{
	Super::NativeOnMouseEnter(Geometry, Event);

	// EVEN WHEN THE BUTTON IS DISABLED. A node that cannot take a point is
	// the node a player most wants to read, because the question they have
	// is why it cannot.
	OnChoiceHovered.Broadcast(Value);
}

void UCataclysmChoiceButton::HandleClicked()
{
	Press();
}
