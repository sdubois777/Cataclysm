// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CataclysmPauseScreenWidget.generated.h"

class ACataclysmPlayerController;
class UButton;

/**
 * The pause screen: "Paused", a Resume button, and a line saying Esc resumes as well. The project owner, 2026-11-24:
 * "Offline and online, pause is a must". Esc with no other screen open shows it, through
 * `ACataclysmPlayerController::PauseTheGame`; Esc again, or Resume, takes it down through `ResumeTheGame`.
 *
 * BUILT IN C++, WITH NO WIDGET BLUEPRINT, as the inventory screen and the ground hover's tooltip are. It holds three
 * words and a button, and a generated `WBP_` asset would need an editor run before the screen could exist at all.
 *
 * IT WORKS WHILE THE GAME IS PAUSED BECAUSE SLATE DOES NOT PAUSE. The engine's pause stops the world: its actors, its
 * timers and its clock. The interface is ticked and clicked by Slate, outside the world, so the button takes a click
 * and the screen draws while nothing behind it moves.
 *
 * IT TAKES EVERY CLICK ON IT, the dimmed rest of the screen included (`NativeOnMouseButtonDown`). Nothing in the world
 * can act on a click while it is paused, since Enhanced Input triggers no action then, but a press that reached the
 * game would still be remembered by the controller's left-button state and acted on after the pause.
 *
 * ONLY RESUME, AND THAT IS WHAT THE GAME HAS. The owner asked for the actions that already exist; the save design says
 * "there is no manual save" (`docs/Save_System_Design.md`, section 6), and no way back to the character select screen
 * exists from inside a level but the hand-over a trip makes, which writes no floor. `docs/DECISIONS.md`, 2026-11-24.
 */
UCLASS()
class CATACLYSM_API UCataclysmPauseScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The words on the screen. Static so a test reads them: a test has no Slate to draw them. */
	static const TCHAR* TitleText;
	static const TCHAR* ResumeText;
	static const TCHAR* HintText;

	/** Above every other screen, so the pause is what a player sees and what a click lands on. */
	static constexpr int32 PauseScreenZOrder = 50;

	/** How much of the game behind it the dim hides, from 0 to 1. A look, and a judgement. */
	static constexpr float DimOpacity = 0.6f;

	/** The controller the Resume button resumes. The controller tells the screen when it makes it. */
	void SetResumer(ACataclysmPlayerController* InResumer);

	/**
	 * What the Resume button does, separated from the click: `UCataclysmChoiceButton::Press`'s reason, a test cannot
	 * click a button. Whether the game runs afterwards; false, and nothing done, with no controller to resume.
	 */
	bool PressResume();

protected:
	//~ UUserWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget

private:
	/** The dim, the panel, the three lines and the button, made once. */
	void BuildPauseTree();

	/** Bound to the button's click. Forwards to `PressResume`. */
	UFUNCTION()
	void HandleResumeClicked();

	/** The Resume button, kept so `NativeConstruct` can bind it. */
	UPROPERTY()
	TObjectPtr<UButton> ResumeButton = nullptr;

	/** Weak: the screen can outlive its controller across a level change, and a click then does nothing. */
	TWeakObjectPtr<ACataclysmPlayerController> Resumer;
};
