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

void UCataclysmChoiceButton::SetLabelScale(float Scale)
{
	// A SCALE OF NOTHING OR LESS IS A MISTAKE ABOVE RATHER THAN A REQUEST FOR
	// INVISIBLE WORDS. `SmallestLabelPoints` catches the rest of the way down.
	LabelScale = FMath::Max(0.01f, Scale);
	RefreshDisplay();
}

int32 UCataclysmChoiceButton::LabelPoints() const
{
	return ChoiceLabel
		? FMath::RoundToInt(static_cast<float>(ChoiceLabel->GetFont().Size))
		: 0;
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

		FSlateFontInfo Font = ChoiceLabel->GetFont();

		// THE BLUEPRINT'S OWN SIZE, READ THE FIRST TIME THERE IS A LABEL TO READ
		// IT FROM. See `DesignedLabelPoints`: reading the current size instead
		// would shrink the words a little more on every redraw.
		if (DesignedLabelPoints <= 0)
		{
			DesignedLabelPoints =
				FMath::RoundToInt(static_cast<float>(Font.Size));
		}

		// ROUNDED DOWN AND NOT TO NEAREST. A screen scales the BOX by exactly
		// the scale it asks for, and a font size can only be a whole number of
		// points, so rounding to nearest can make the words a LARGER share of
		// their designed size than the box is of its designed size -- at a scale
		// of 0.8, 16 points rounds to 13, which is 0.81. Measured on 2026-08-31:
		// that alone put "Outpost 100%" 11 pixels outside a box it fitted at
		// full size. Rounding down can only ever leave room.
		const int32 Points = FMath::Max(
			SmallestLabelPoints,
			FMath::FloorToInt(DesignedLabelPoints * LabelScale));

		// ONLY WHEN IT MOVED. `SetFont` rebuilds the text block's layout, and
		// this runs on every redraw of a screen holding 25 of these.
		if (FMath::RoundToInt(static_cast<float>(Font.Size)) != Points)
		{
			Font.Size = Points;
			ChoiceLabel->SetFont(Font);
		}
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
