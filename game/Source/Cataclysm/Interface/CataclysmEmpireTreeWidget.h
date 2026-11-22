// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Character/CataclysmPassiveTree.h"
#include "CataclysmEmpireTreeWidget.generated.h"

class UButton;
class UCanvasPanel;
class UCataclysmChoiceButton;
class UDataTable;
class UImage;
class UPanelWidget;
class UTextBlock;
struct FCataclysmHeldEmpireTree;

/**
 * The empire upgrade tree screen: see the bank and the tree, buy points, choose a centre capstone's option, respec.
 * Issue #2741, slice 2. `docs/Cataclysm_GDD_v2.md`, Empire-Wide Upgrades.
 *
 * THE CLASS TREE SCREEN'S SHAPE (`UCataclysmPassiveTreeWidget`): the logic and state here in C++, where a headless test
 * reaches them, and the layout in `WBP_EmpireTree`, made by `tools/generate_interface_assets.py`. One view at a time,
 * chosen from a list -- the four branches and the centre's four capstones -- drawn on a canvas as one button per node
 * and one line per edge, with the mouse wheel zooming, the right button panning, and F fitting the view. A click on a
 * centre capstone that has opened puts its three options in the list in place of the views. The arithmetic and every
 * word are `UCataclysmEmpireTreeLayout`'s.
 *
 * IT READS ONE THING AND CHANGES IT THROUGH THREE. The tree and bank are `UCataclysmGameInstance::EmpireTreeHeldBy`'s
 * -- the lethality mode's, or a Solo Self-Found character's own -- and every change goes through
 * `BuyEmpireTreePoint`, `ChooseEmpireTreeOption` or `RespecEmpireTree`, which save, say the PLAY LOOP lines, and refuse
 * during a walk (`EmpireTreeChangeRefusal`). So the screen holds no tree of its own and asks no question about which
 * record holds it.
 *
 * IT OPENS DURING A WALK, TO BE READ. The coordinating session's S2 of 2026-11-18: the map it opens from opens in a
 * dungeon too, and the walk refusal is the line it shows there, under the branch, whenever nothing else was refused.
 *
 * RESPEC TAKES TWO PRESSES, S1 of the same day: the first says what it costs ("Press again to respec: 5 days of the
 * run"), the second does it, and any other press cancels. A respec the rules would refuse says so on the first press
 * and asks for no second. THE BUTTON IS HERE UNTIL THE TRAINER EXISTS: the design puts respec at the capital's Trainer
 * ("Respec class passive skill points, and respec the empire upgrade tree. Both cost days."), which is not built.
 *
 * EVERY FUNCTION WORKS WITH NO WIDGETS AT ALL, for the class tree screen's reason: the automation command passes
 * `-nullrhi`, so a test can make this class but not its Blueprint, and every bound pointer is null in a test.
 */
UCLASS()
class CATACLYSM_API UCataclysmEmpireTreeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Which view is shown: a branch's name, or `UCataclysmEmpireTree::CentreBranch`. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FString GetShownBranch() const { return ShownBranch; }

	/** Show this view. Clears the last refusal and the respec's first press. @return false for a name no view has. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	bool ShowBranch(const FString& Branch);

	/** The nodes of the view shown, in the order `UCataclysmEmpireTree::NodesIn` gives. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	TArray<FName> ShownNodes() const;

	/** Buy one point in a node through the game instance, and redraw. `RefusalText` says why when refused. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	bool BuyPoint(FName Node);

	/** Choose one of a centre capstone's options through the game instance, and redraw. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	bool ChooseOption(FName Node, int32 Option);

	/** Take Imperial Prowess's extra option -- one more option of an earlier centre milestone -- through the game
	 *  instance, and redraw. Issue #2741 slice 8. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	bool ChooseExtraOption(FName Node, int32 Option);

	/** Whether the options in the list are offered as Imperial Prowess's extra option rather than as the choice. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	bool IsChoosingAnExtraOption() const { return bChoosingExtraOption; }

	/**
	 * What a click on a node means: on a centre capstone that has opened and awaits a choice, its three options are put
	 * in the list (and nothing is bought); on an earlier centre milestone while Imperial Prowess's extra option is open
	 * there, its options are put in the list as that extra option (issue #2741 slice 8); on any other node, a point is
	 * bought. Public so a test can drive the click,
	 * for the reason `UCataclysmPassiveTreeWidget::TouchNode` gives.
	 *
	 * @return true when a point was bought or the options were put in the list
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	bool TouchNode(FName Node);

	/**
	 * A press of the Respec button. The first press asks for a second, saying what it costs; the second respecs through
	 * `UCataclysmGameInstance::RespecEmpireTree`. A respec the rules refuse is refused on the first press.
	 *
	 * @return true only on the press that respecced
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	bool PressRespec();

	/** Whether the Respec button is waiting for its second press. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	bool IsAwaitingTheSecondRespecPress() const { return bAwaitingTheSecondRespecPress; }

	/** The centre capstone whose three options are in the list, or none. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FName GetCapstoneAwaitingAChoice() const { return ChoosingOptionFor; }

	/** A capstone option as a value the list's buttons can carry, and back; `UCataclysmPassiveTreeWidget`'s scheme. */
	static FName OptionValue(int32 Option);
	static int32 OptionFromValue(FName Value);

	//~ What the screen says, through `UCataclysmEmpireTreeLayout`.

	/** The bank, or why there is no tree to show. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FText BankText() const;

	/** The view shown and the points in it and in the tree. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FText BranchText() const;

	/** Why the last thing tried was refused; when nothing was, the walk refusal during a walk; else empty. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FText RefusalText() const;

	/** What the Respec button says. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FText RespecText() const;

	/** What the line under the branch says about the last node touched or hovered, or the capstone being decided. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FText DescriptionText() const;

	/** The days a respec would take off the run now: the run's death cost, or 0 with no run. */
	int32 RespecDaysNow() const;

	/** Rewrite every bound widget from the current state. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	void RefreshDisplay();

	/** Use these tables rather than the generated ones. For tests. */
	void SetTables(const UDataTable* InNodeTable, const UDataTable* InEdgeTable);

	//~ Looking around the view, as on the class tree screen.

	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	float GetZoom() const { return Zoom; }

	UFUNCTION(BlueprintPure, Category = "Cataclysm|EmpireTree")
	FVector2D GetFocus() const { return Focus; }

	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	void ZoomBy(float Notches);

	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	void PanBy(FVector2D Pixels);

	/** Put the whole of the view shown inside the panel. What opening the screen does, and what F does. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|EmpireTree")
	void FitToBranch();

	//~ UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	//~ End UUserWidget

	/**
	 * Read the tree of the character playing in this world rather than in the owning player's. FOR TESTS: a widget a
	 * headless test makes has no owning player, and `EmpireTreeHeldBy` needs a world with a character in it.
	 */
	void SetWorldContextForTests(UObject* InContext);

	/** Say how big the panel is instead of asking Slate. For tests; zero puts it back to asking. */
	void SetPanelSizeForTests(FVector2D Size);

	/** How the node buttons for the view shown are described, for a test that made its own button. */
	void DescribeNodeButtonForTests(UCataclysmChoiceButton& Button, FName Node);

protected:
	/** Where the five views' buttons go, or a capstone's three options while one is being decided. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> BranchBox;

	/** Where the view is drawn: one button per node, one line per edge. Filled by this class. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> GraphCanvas;

	/** The bank. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BankLabel;

	/** The view and the points in it. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BranchLabel;

	/** What the node under the cursor, or the last touched, says. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DescriptionLabel;

	/** Why the last thing tried was refused. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	/** The Respec button, and the words on it. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RespecButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RespecLabel;

	/** Closes the screen (`ACataclysmPlayerController::ToggleEmpireTree`). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	/** The screen's heading, decoration and so optional, as on the class tree screen. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/** What one entry in either list is drawn as, and each node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|EmpireTree")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|EmpireTree")
	FText Title = NSLOCTEXT("Cataclysm", "EmpireTreeTitle", "Empire upgrade tree");

	/** The colour an edge is drawn in, quieter than a node, as on the class tree screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|EmpireTree")
	FLinearColor EdgeColour = FLinearColor(0.35f, 0.39f, 0.44f, 1.0f);

private:
	UFUNCTION()
	void HandleBranchClicked(FName Value);

	UFUNCTION()
	void HandleNodeClicked(FName Node);

	UFUNCTION()
	void HandleNodeHovered(FName Node);

	UFUNCTION()
	void HandleRespecClicked();

	UFUNCTION()
	void HandleCloseClicked();

	const UDataTable* NodeTable() const;
	const UDataTable* EdgeTable() const;

	/** What `EmpireTreeHeldBy` is asked with: the test's context, else this widget. */
	const UObject* Context() const;

	/** The tree held, as `EmpireTreeHeldBy` answers it; false with nothing held. */
	bool Held(FCataclysmHeldEmpireTree& OutHeld) const;

	/** The tree held, or an empty one when there is none. */
	const FCataclysmPassiveAllocation& Allocation() const;

	/** `ChoiceButtonClass` loaded, or null with one complaint in the log. */
	UClass* LoadedChoiceButtonClass();

	/** Fill the list with the five views, or with a capstone's three options while one is being decided. */
	void FillList();

	/** Make one button per node and one line per edge for the view shown. */
	void BuildGraph();

	/** Move every node and edge to where it belongs at the current zoom and focus, and relabel the nodes. */
	void PlaceGraph();

	/** One node's label, whether it is drawn dimmed, and the reason on its tool tip. */
	void DescribeNodeButton(UCataclysmChoiceButton& Button, FName Node);

	/** The canvas's size in pixels, the test's, or a sensible guess before Slate has laid it out. */
	FVector2D CanvasSize() const;

	FString ShownBranch;
	FString LastRefusal;
	FName LastTouched;
	bool bAwaitingTheSecondRespecPress = false;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> Nodes;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> Edges;

	UPROPERTY(Transient)
	TSubclassOf<UCataclysmChoiceButton> ResolvedChoiceButtonClass;

	UPROPERTY(Transient)
	TObjectPtr<UObject> ContextForTests;

	bool bComplainedAboutTheChoiceButtonClass = false;

	float Zoom = 1.0f;
	FVector2D Focus = FVector2D::ZeroVector;
	FVector2D FittedAgainst = FVector2D::ZeroVector;
	FVector2D PanelSizeForTests = FVector2D::ZeroVector;

	/** Which view the canvas holds widgets for, and how big a node may be drawn in it. */
	FString BuiltBranch;
	float BuiltNodeScale = 1.0f;

	UPROPERTY(Transient)
	FName ChoosingOptionFor;

	/** Whether `ChoosingOptionFor`'s options are offered as Imperial Prowess's extra option. Issue #2741 slice 8. */
	UPROPERTY(Transient)
	bool bChoosingExtraOption = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCataclysmChoiceButton>> NodeButtons;

	UPROPERTY(Transient)
	TArray<FName> NodeButtonNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> EdgeImages;

	TArray<TPair<FName, FName>> EdgeEnds;

	FVector2D DragFrom = FVector2D::ZeroVector;
	bool bDragging = false;
};
