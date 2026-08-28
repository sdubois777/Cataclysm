// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Character/CataclysmPassiveTree.h"
#include "CataclysmPassiveTreeWidget.generated.h"

class UButton;
class ACataclysmPlayerState;
class UCanvasPanel;
class UCataclysmChoiceButton;
class UDataTable;
class UPanelWidget;
class UTextBlock;

/**
 * The passive class tree screen: see the points, and spend them.
 *
 * `docs/Cataclysm_GDD_v2.md`, the Passive Class Trees section. Part of issue #50.
 *
 * THE SECOND SCREEN BUILT THE WAY `docs/DECISIONS.md` DECIDED ON 2026-08-24, and
 * the same split as the character creator: the logic and the state here in C++,
 * where a diff shows them and a headless test can reach them, and the layout in
 * `WBP_PassiveTree` under `game/Content/Interface/`.
 *
 * WHAT IT DRAWS, STATED PLAINLY: a list. One row per node, in the order the
 * nodes are laid out in the authoring tool, top to bottom. It does NOT draw the
 * tree as a graph with lines between nodes, and the decision that put layout in
 * a Widget Blueprint gave "a large node graph with panning and zooming" as one
 * of its reasons. Issue #937 is the graph. A list is what makes the points
 * spendable and the rules visible today, and everything it needs from C++ --
 * which nodes exist, where they sit, what is shut and why -- is what a graph
 * would need too, so it is not work that gets thrown away.
 *
 * WHY EVERY FUNCTION WORKS WITH NO WIDGETS AT ALL. The automation command in
 * `tools/unreal_build.py` passes `-nullrhi` and runs with no editor, so a test
 * can construct this class but cannot give it a Blueprint, and every bound
 * pointer is therefore null in a test. Each is checked before it is touched.
 *
 * WHAT A SPENT POINT IS WORTH IS NOT SHOWN, THOUGH 26 NODES NOW HAVE A NUMBER.
 * Each row carries the node's own description, which is the sentence a player
 * reads; the screen does not say which nodes are backed by an authored effect
 * and which are still words only. That is worth showing and is not built:
 * 26 of the 293 nodes grant something, so a player spending on one of the other
 * 267 gets nothing and the screen gives them no way to tell. Issues #936 and
 * #939.
 */
UCLASS()
class CATACLYSM_API UCataclysmPassiveTreeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Which tree is being looked at. Empty until one is chosen. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	FString GetShownTree() const { return ShownTree; }

	/**
	 * Look at this tree.
	 *
	 * A TREE THE CHARACTER CANNOT REACH CAN STILL BE LOOKED AT, and only
	 * spending in it is refused. A player deciding which weapon to carry needs
	 * to be able to read what the other trees offer, and a screen that hid them
	 * would make that decision blind.
	 *
	 * @return false for a name no tree has
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	bool ShowTree(const FString& Tree);

	/** Every tree there is, in alphabetical order. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	TArray<FString> AllTrees() const;

	/** Which of them this character's damage type unlocks. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	TArray<FString> ReachableTrees() const;

	/** The nodes of the tree being looked at, in the order shown. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	TArray<FName> ShownNodes() const;

	/**
	 * Put a point into a node, and redraw.
	 *
	 * @return false and changes nothing when the character has no player state,
	 *         or when the spend was refused. `RefusalText` then says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	bool SpendInto(FName Node);

	/** Take one of a capstone's three options, and redraw. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	bool ChooseOption(FName Node, int32 Option);

	/**
	 * What clicking a node on the graph means.
	 *
	 * TWO THINGS A CLICK CAN MEAN, AND UNTIL ISSUE #1064 IT ONLY EVER MEANT ONE.
	 * `HandleNodeClicked` called `SpendInto` and nothing else, so clicking a
	 * capstone always tried to spend a point -- which is refused until one of
	 * its three options is taken. Nothing on the screen ever called
	 * `ChooseOption`, so no capstone in any tree could be taken from the screen
	 * at all. The project owner crossed the first capstone's threshold and found
	 * it still drawn as locked.
	 *
	 * A CLICK ON A CAPSTONE THAT HAS OPENED NOW OFFERS ITS THREE OPTIONS instead
	 * of spending, and a click on anything else spends as it always did.
	 *
	 * IT NEVER COMMITS A CHOICE. The choice is permanent -- every capstone's own
	 * description says so -- so a player has to see the three names and pick
	 * one, which is a second click.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT. `HandleNodeClicked` is private and bound by
	 * reflection to a button that no headless test can press, which is why a
	 * click path that could never take a capstone went unnoticed: the two
	 * existing screen tests call `SpendInto` and `ChooseOption` directly and
	 * neither goes near the decision between them.
	 *
	 * @return true when the click did something -- a point was spent, or the
	 *         options were opened
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	bool TouchNode(FName Node);

	/**
	 * The capstone whose three options are being shown, or none.
	 *
	 * WHERE THEY ARE SHOWN. In the tree list, in place of the four tree names,
	 * because that is the only other list of buttons on this screen and building
	 * a third would be more machinery than the decision needs. Clicking any node
	 * on the graph puts the tree names back.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	FName GetCapstoneAwaitingAChoice() const { return ChoosingOptionFor; }

	/**
	 * A capstone option as a value the tree list's buttons can carry, and back.
	 *
	 * ONE LIST AND ONE HANDLER CARRY BOTH, so the two kinds of value have to be
	 * told apart. A tree's value is its name out of the node table's `Tree`
	 * column -- Masochist, Bulwark -- and an option's begins with a space, which
	 * no tree name can. `OptionFromValue` answers 0 for anything that is not an
	 * option, which is every tree.
	 */
	static FName OptionValue(int32 Option);
	static int32 OptionFromValue(FName Value);

	//~ What the screen says.

	/** How many points are earned, spent and left. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	FText PointsText() const;

	/** Which tree is shown, how much is in it, and whether it is reachable. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	FText TreeText() const;

	/** Why the last spend was refused, or empty. Cleared by a spend that works. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	FText RefusalText() const { return FText::FromString(LastRefusal); }

	/** Rewrite every bound widget from the current state. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	void RefreshDisplay();

	/** Use these tables rather than the generated ones. For tests. */
	void SetTables(const UDataTable* InNodeTable, const UDataTable* InEdgeTable);

	//~ Looking around the tree.

	/**
	 * How far the view is scaled. One draws the tree at its authored size.
	 *
	 * FITTED TO THE PANEL WHEN A TREE IS FIRST SHOWN, rather than starting at
	 * one. The Masochist tree is 3,600 units across and a panel is about 2,400
	 * pixels, so a zoom of one would put a third of it off each side and the
	 * player would open the screen looking at the middle of a limb.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	float GetZoom() const { return Zoom; }

	/** Which point of the tree sits in the middle of the panel. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	FVector2D GetFocus() const { return Focus; }

	/** Scale the view by this many mouse wheel notches. Positive zooms in. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	void ZoomBy(float Notches);

	/** Move the view by this many pixels of the panel. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	void PanBy(FVector2D Pixels);

	/**
	 * Put the whole of the tree being shown inside the panel.
	 *
	 * WHAT OPENING THE SCREEN DOES, and what the F key does afterwards. A player
	 * who has panned into a limb needs a way back that does not involve
	 * scrolling until something familiar appears.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	void FitToTree();

	//~ UUserWidget
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,
									  const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,
										   const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry,
									 const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,
										 const FPointerEvent& Event) override;
	//~ End UUserWidget

	/**
	 * Read and write this player state rather than the owning player's.
	 *
	 * FOR TESTS, AND THE ENGINE LEAVES NO OTHER WAY. `CreateWidget` refuses a
	 * controller that is not a LOCAL player controller -- "Only Local Player
	 * Controllers can be assigned to widgets" -- and a world built by
	 * `UWorld::CreateWorld` has no game instance, so it can have no local
	 * player. Without this the only thing a headless test could reach is a
	 * widget with no character behind it, and every question this screen answers
	 * is a question about a character.
	 *
	 * IT IS NOT A SECOND SOURCE OF TRUTH. Whatever it is given is the same
	 * player state the owning player would have supplied, and it is only
	 * consulted when there is no owning player.
	 */
	void SetPlayerStateForTests(ACataclysmPlayerState* InState);

protected:
	/** Where the tree-selector buttons go. Filled by this class. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> TreeBox;

	/**
	 * Where the tree is drawn: one button per node, one line per edge.
	 *
	 * A CANVAS PANEL AND NOT A LIST, since issue #937. Every node is placed at
	 * its own authored position, scaled and panned, so the tree keeps the shape
	 * it was drawn in. Which limb a node is on and how far it is from the trunk
	 * are decisions made in `C:\Projects\PassiveTreeCreator`, and a list threw
	 * all of that away.
	 *
	 * THE DESIGNER PLACES THE PANEL AND THIS CLASS FILLS IT. Where the panel
	 * sits, how big it is and what is around it are layout; where a node lands
	 * inside it is arithmetic that no designer could do by hand for 74 nodes.
	 * `UCataclysmPassiveTreeLayout` is that arithmetic and it is covered by
	 * tests.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> GraphCanvas;

	/** Earned, spent and left. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PointsLabel;

	/** Which tree, how much is in it, whether it can be spent in. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TreeLabel;

	/** Why the last thing tried was refused. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefusalLabel;

	/** What the node under the cursor says it does. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DescriptionLabel;

	/**
	 * The screen's heading. `BindWidgetOptional` because a heading is
	 * decoration, and it is the only optional one for that reason.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	/** What one entry in either list is drawn as. See the character creator's
	 *  property of the same shape for why this is a soft path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Passives")
	TSoftClassPtr<UCataclysmChoiceButton> ChoiceButtonClass =
		TSoftClassPtr<UCataclysmChoiceButton>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoiceButton.WBP_ChoiceButton_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Passives")
	FText Title = NSLOCTEXT("Cataclysm", "PassiveTitle", "Passive tree");

	/**
	 * The colour a dependency line between two nodes is drawn in.
	 *
	 * QUIET ON PURPOSE. The lines are structure and the nodes are the thing
	 * being read, so a line as bright as a node would compete with the words it
	 * is there to connect. `EditAnywhere`, so a designer opening the Blueprint
	 * owns the actual colour: which colour it is is a look, and that a line is
	 * quieter than a node is the judgement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Passives")
	FLinearColor EdgeColour = FLinearColor(0.35f, 0.39f, 0.44f, 1.0f);

private:
	/** Bound to every tree-selector button. */
	UFUNCTION()
	void HandleTreeClicked(FName Tree);

	/** Bound to every node button. */
	UFUNCTION()
	void HandleNodeClicked(FName Node);


	/**
	 * Bound to every node button's hover.
	 *
	 * READING A NODE MUST NOT COST A POINT. A node is far too small at a
	 * fitted zoom to hold its own description, so the description under the
	 * tree follows the cursor instead. Spending stays on the click.
	 */
	UFUNCTION()
	void HandleNodeHovered(FName Node);

	const UDataTable* NodeTable() const;
	const UDataTable* EdgeTable() const;

	/** The owning character's player state, the one given for a test, or null. */
	ACataclysmPlayerState* State() const;

	/** `ChoiceButtonClass` loaded, or null with one complaint in the log. */
	UClass* LoadedChoiceButtonClass();

	/** Empty a panel and refill it, or update what is already in it. */
	void FillPanel(UPanelWidget* Panel, const TArray<FName>& Values,
				   bool bTrees);

	/**
	 * Place one button per node and one line per edge on the canvas.
	 *
	 * REBUILT WHEN THE TREE CHANGES AND MOVED WHEN THE VIEW DOES. Making 74
	 * buttons and 69 lines is work for a click; moving them is work for a drag,
	 * which happens every frame the mouse is down. So the two are separate.
	 */
	void BuildGraph();

	/** Move every node and edge already on the canvas to where it now belongs. */
	void PlaceGraph();

	/** What one node's button should say and whether it can take a point. */
	void DescribeNodeButton(class UCataclysmChoiceButton& Button, FName Node);

	/** The canvas's size in pixels, or a sensible guess before it has one. */
	FVector2D CanvasSize() const;

	FString ShownTree;
	FString LastRefusal;

	/** The last node a spend was tried on, so its description can be shown. */
	FName LastTouched;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> Nodes;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> Edges;

	UPROPERTY(Transient)
	TSubclassOf<UCataclysmChoiceButton> ResolvedChoiceButtonClass;

	UPROPERTY(Transient)
	TObjectPtr<ACataclysmPlayerState> StateForTests;

	bool bComplainedAboutTheChoiceButtonClass = false;

	/** How far the view is scaled, and what is in the middle of it. */
	float Zoom = 1.0f;
	FVector2D Focus = FVector2D::ZeroVector;

	/** Which tree the canvas currently holds widgets for. Empty for none. */
	FString BuiltTree;

	/**
	 * How big a node may be drawn in the tree on the canvas, as a factor.
	 *
	 * WORKED OUT ONCE WHEN THE TREE IS BUILT, because it walks every pair
	 * of nodes and does not change while one tree is being looked at.
	 */
	float BuiltNodeScale = 1.0f;

	/**
	 * The capstone whose three options are in the tree list, or NAME_None.
	 * Issue #1064. See `GetCapstoneAwaitingAChoice`.
	 *
	 * CLEARED BY A CLICK ON ANY NODE, which is what puts the tree names back and
	 * is also what keeps the rebuild safe: the tree list is emptied and refilled
	 * from a click on a button that lives on the GRAPH, never from one of its
	 * own children. Emptying a panel from inside its own child's click handler
	 * tears down the Slate widget whose event is still being dispatched, which
	 * is the hazard `FillPanel` already warns about.
	 *
	 * SO CHOOSING AN OPTION LEAVES THE LIST ALONE. The three values do not
	 * change when one of them is taken, and `FillPanel` rebuilds only when the
	 * values differ, so the buttons are relabelled in place.
	 */
	UPROPERTY(Transient)
	FName ChoosingOptionFor;

	/** The node each button on the canvas is for, in the order they were made. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UCataclysmChoiceButton>> NodeButtons;

	UPROPERTY(Transient)
	TArray<FName> NodeButtonNames;

	/** The edge images, and which two nodes each joins. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UImage>> EdgeImages;

	TArray<TPair<FName, FName>> EdgeEnds;

	/** Where the mouse was when a drag began. Panning is a drag. */
	FVector2D DragFrom = FVector2D::ZeroVector;
	bool bDragging = false;
};
