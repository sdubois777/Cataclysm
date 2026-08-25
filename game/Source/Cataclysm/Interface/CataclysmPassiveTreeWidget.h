// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Character/CataclysmPassiveTree.h"
#include "CataclysmPassiveTreeWidget.generated.h"

class UButton;
class ACataclysmPlayerState;
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
 * WHAT A SPENT POINT IS WORTH IS NOT SHOWN, BECAUSE IT DOES NOT EXIST. Each row
 * carries the node's own description, which is a sentence written for a player.
 * There is no number behind it anywhere in the design files, so nothing applies
 * it to the character. Issue #936.
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

	//~ UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

protected:
	/** Where the tree-selector buttons go. Filled by this class. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> TreeBox;

	/** Where one button per node of the shown tree goes. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> NodeBox;

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

private:
	/** Bound to every tree-selector button. */
	UFUNCTION()
	void HandleTreeClicked(FName Tree);

	/** Bound to every node button. */
	UFUNCTION()
	void HandleNodeClicked(FName Node);

	const UDataTable* NodeTable() const;
	const UDataTable* EdgeTable() const;

	/** The owning character's player state, the one given for a test, or null. */
	ACataclysmPlayerState* State() const;

	/** `ChoiceButtonClass` loaded, or null with one complaint in the log. */
	UClass* LoadedChoiceButtonClass();

	/** Empty a panel and refill it, or update what is already in it. */
	void FillPanel(UPanelWidget* Panel, const TArray<FName>& Values,
				   bool bTrees);

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
};
