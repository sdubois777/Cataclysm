// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmPauseScreenWidget.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Player/CataclysmPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Styling/SlateTypes.h"

/**
 * How the pause screen looks. A NAMED NAMESPACE AND NOT AN ANONYMOUS ONE: the module is built as a unity build, which
 * joins its .cpp files, and two anonymous namespaces holding one name collide
 * (`tools/tests/test_no_two_files_share_an_anonymous_helper.py`). Every figure here is a look, and a judgement.
 */
namespace CataclysmPauseScreenLook
{
	constexpr float TitleFontPx = 28.0f;
	constexpr float ButtonFontPx = 18.0f;
	constexpr float HintFontPx = 12.0f;
	constexpr float PanelPaddingPx = 32.0f;
	constexpr float GapPx = 16.0f;
	constexpr float ButtonPaddingPx = 10.0f;
	constexpr float HintOpacity = 0.7f;

	/** The button in its three states: solid colours, so it reads the same whatever the engine's default style is. */
	const FLinearColor ButtonNormal(0.20f, 0.18f, 0.15f, 1.0f);
	const FLinearColor ButtonHovered(0.32f, 0.28f, 0.22f, 1.0f);
	const FLinearColor ButtonPressed(0.13f, 0.12f, 0.10f, 1.0f);
}

const TCHAR* UCataclysmPauseScreenWidget::TitleText = TEXT("Paused");
const TCHAR* UCataclysmPauseScreenWidget::ResumeText = TEXT("Resume");
const TCHAR* UCataclysmPauseScreenWidget::HintText = TEXT("Esc resumes as well");

void UCataclysmPauseScreenWidget::SetResumer(ACataclysmPlayerController* InResumer)
{
	Resumer = InResumer;
}

bool UCataclysmPauseScreenWidget::PressResume()
{
	ACataclysmPlayerController* Controller = Resumer.Get();
	if (!Controller)
	{
		return false;
	}
	return Controller->ResumeTheGame();
}

TSharedRef<SWidget> UCataclysmPauseScreenWidget::RebuildWidget()
{
	// ONCE: the tree is kept for as long as the widget is, and `UUserWidget::Initialize` has made `WidgetTree` by the
	// time this runs. The inventory widget's `RebuildWidget` gives the one case it has not.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildPauseTree();
	}
	return Super::RebuildWidget();
}

void UCataclysmPauseScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ResumeButton)
	{
		// CLEARED FIRST: `NativeConstruct` runs every time the screen is added to the viewport, and a delegate added
		// twice fires twice. `UCataclysmChoiceButton::NativeConstruct` gives the same reason.
		ResumeButton->OnClicked.RemoveAll(this);
		ResumeButton->OnClicked.AddDynamic(this, &UCataclysmPauseScreenWidget::HandleResumeClicked);
	}
}

FReply UCataclysmPauseScreenWidget::NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	// EVERY CLICK ON THE SCREEN IS THE SCREEN'S. A click on the button never reaches here: the button is under the
	// cursor first and takes it. A click on the dim would otherwise go on to the game viewport.
	return FReply::Handled();
}

void UCataclysmPauseScreenWidget::HandleResumeClicked()
{
	PressResume();
}

void UCataclysmPauseScreenWidget::BuildPauseTree()
{
	namespace Look = CataclysmPauseScreenLook;

	// HIT-TESTED ITSELF, which a user widget is not by default (`SelfHitTestInvisible`), so a click on the dim lands
	// on this widget and `NativeOnMouseButtonDown` takes it.
	SetVisibility(ESlateVisibility::Visible);

	// THE DIM OVER THE WHOLE GAME, holding the panel in its middle.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PauseDim"));
	Dim->SetBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, DimOpacity)));
	Dim->SetHorizontalAlignment(HAlign_Center);
	Dim->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Dim;

	// THE PANEL, IN THE CARRIED INVENTORY'S COLOURS, so the two screens read as one interface.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PausePanel"));
	Panel->SetBrush(FSlateColorBrush(UCataclysmInventoryScreen::PanelColour()));
	Panel->SetPadding(FMargin(Look::PanelPaddingPx));
	Dim->SetContent(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseColumn"));
	Panel->SetContent(Column);

	const FLinearColor Ink = UCataclysmInventoryScreen::InkColour();

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PauseTitle"));
	Title->SetText(FText::FromString(TitleText));
	Title->SetFontSize(Look::TitleFontPx);
	Title->SetColorAndOpacity(FSlateColor(Ink));
	Title->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Center);
	TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Look::GapPx));

	// THE BUTTON'S OWN STYLE, solid colours in its three states, rather than the engine's default button, whose look
	// the label's ink would have to be guessed against.
	FButtonStyle Style;
	Style.SetNormal(FSlateColorBrush(Look::ButtonNormal));
	Style.SetHovered(FSlateColorBrush(Look::ButtonHovered));
	Style.SetPressed(FSlateColorBrush(Look::ButtonPressed));
	Style.SetNormalPadding(FMargin(Look::ButtonPaddingPx));
	Style.SetPressedPadding(FMargin(Look::ButtonPaddingPx));

	ResumeButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PauseResume"));
	ResumeButton->SetStyle(Style);
	UTextBlock* ResumeLabel =
		WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PauseResumeLabel"));
	ResumeLabel->SetText(FText::FromString(ResumeText));
	ResumeLabel->SetFontSize(Look::ButtonFontPx);
	ResumeLabel->SetColorAndOpacity(FSlateColor(Ink));
	ResumeLabel->SetJustification(ETextJustify::Center);
	ResumeButton->SetContent(ResumeLabel);
	UVerticalBoxSlot* ButtonSlot = Column->AddChildToVerticalBox(ResumeButton);
	ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Look::GapPx));

	FLinearColor Faint = Ink;
	Faint.A = Look::HintOpacity;
	UTextBlock* Hint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PauseHint"));
	Hint->SetText(FText::FromString(HintText));
	Hint->SetFontSize(Look::HintFontPx);
	Hint->SetColorAndOpacity(FSlateColor(Faint));
	Hint->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* HintSlot = Column->AddChildToVerticalBox(Hint);
	HintSlot->SetHorizontalAlignment(HAlign_Center);
}

// ---------------------------------------------------------------------------
// Cataclysm.PauseGame: pauses the game, or lets it run again, as Esc does with no screen open. For a checkout or an
// editor where Esc does not reach the game: in Play In Editor the editor's Stop takes Esc unless
// `game/Config/DefaultEditorSettings.ini` has moved Stop to Shift+Esc. The owner's rule of 2026-11-24.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmPauseGame(
	TEXT("Cataclysm.PauseGame"),
	TEXT("Pause the game and show the pause screen, or, when it is paused, let it run again. Esc does the same when no "
		 "screen is open."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			ACataclysmPlayerController* Controller =
				Cast<ACataclysmPlayerController>(World ? World->GetFirstPlayerController() : nullptr);
			if (!Controller)
			{
				Ar.Log(TEXT("There is no Cataclysm player controller, so there is no game here to pause."));
				return;
			}
			if (Controller->IsPaused())
			{
				Ar.Log(Controller->ResumeTheGame() ? TEXT("The game runs again.")
												   : TEXT("The game is still paused: something else holds the pause."));
				return;
			}
			Ar.Log(Controller->PauseTheGame() ? TEXT("The game is paused. Esc, Resume or this command lets it run again.")
											  : TEXT("The game could not be paused; the log says why."));
		}));
