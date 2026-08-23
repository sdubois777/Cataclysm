// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "CataclysmPlayerController.generated.h"

class ACataclysmDroppedItem;
class ACataclysmPlayerCharacter;
class UCataclysmAbilitySystemComponent;
class UCataclysmInputConfig;
class UCataclysmInventoryWidget;
class UInputMappingContext;
struct FInputActionValue;

// THE UNDERLYING TYPE IS PART OF THE DECLARATION for a scoped enum, and it
// has to match CataclysmWearing.h exactly or the two are different types.
// Declared rather than included, because the header is only needed for one
// parameter and including it here would pull the item system into every
// translation unit that knows about the controller.
enum class ECataclysmWearResult : uint8;

/**
 * The player's controller: it owns input, the cursor, and the movement order.
 *
 * WHAT THE LEFT MOUSE BUTTON DOES, WHICH IS A DESIGN DECISION AND NOT AN
 * ACCIDENT OF THE CODE. It moves, and only moves. It does not fire the basic
 * attack. The design document's combat section says basic attacks are handled
 * automatically, and the ability slot enum says the same, so there is no second
 * job for this button to do and no ground-versus-enemy disambiguation to get
 * wrong. Clicking an enemy walks toward it like clicking anything else.
 *
 * Recorded in docs/DECISIONS.md. The automatic basic attack itself is issue #36
 * and cannot be built yet, because attack speed still has no base value
 * anywhere and therefore has no rate to fire at -- that is issue #120.
 *
 * Holding the button steers continuously toward the cursor; a short press paths
 * to the point clicked. That split is the shape Unreal's own top-down template
 * uses and matches what the genre does.
 */
UCLASS(Config = Game)
class CATACLYSM_API ACataclysmPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACataclysmPlayerController();

	virtual void SetupInputComponent() override;
	virtual void OnUnPossess() override;

	/**
	 * Runs after the frame's input has been gathered. This is where a slot press
	 * becomes an ability activation; see ProcessAbilityInput for why it is not
	 * done in the key handler itself.
	 */
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

	/**
	 * Whether the cursor is over an interface screen that is open right now.
	 *
	 * ASKS THE WIDGET, which knows its own geometry. Answers false when the
	 * screen has never been opened, when it is closed, and when there is no
	 * mouse.
	 *
	 * PUBLIC SINCE ISSUE #740, AND THE SECOND CALLER IS WHY. The heads-up
	 * display has to ask it before describing the creature under the cursor:
	 * without that, a cursor resting on an inventory cell would describe
	 * whatever creature stands behind the panel. It is the same fault this
	 * function was written for -- a click going through an open screen to the
	 * floor behind it, issue #731 -- asked about hovering rather than clicking.
	 */
	bool CursorIsOverInterface() const;

	/**
	 * Acts on a LEFT press that landed on the open inventory screen.
	 *
	 * Left picks an item up onto the cursor and puts it down again, so the
	 * grid can be rearranged. Right wears and takes off, which is what left
	 * used to do. Issues #831 and #853.
	 *
	 * WHY THE TWO BUTTONS ARE SPLIT THIS WAY. Path of Exile, Last Epoch and
	 * Diablo all do it, so it is what a player expects without being told, and
	 * `docs/Inventory_Screen_Design.md` settles it for this game.
	 *
	 * HERE RATHER THAN IN THE WIDGET, and that is the arrangement the
	 * screen already had. Nothing in the widget's tree consumes a mouse
	 * event -- see UCataclysmInventoryWidget's header -- so the controller
	 * is what learns about the click, and it already had to ask whether the
	 * press landed on the screen before deciding it was not a move order.
	 * Asking which cell is one more question of the same kind.
	 *
	 * WHAT IT DECIDES IS NOWHERE NEAR HERE. UCataclysmWearing holds the
	 * rule for moving an item between the bag and the body without ever
	 * losing one, and it is covered by automation tests. This function
	 * finds the slot and calls it.
	 */
	void PressOnTheInventoryScreen();

	/**
	 * Acts on a RIGHT press that landed on the open inventory screen.
	 *
	 * Wears what is in a carried cell, or takes a worn piece off into the
	 * bag. This is what a left press did before issue #853.
	 */
	void RightPressOnTheInventoryScreen();

	/**
	 * The cursor's place on screen and the character it is acting on, or
	 * false when there is no open screen, no cursor or no pawn.
	 *
	 * SHARED BY BOTH BUTTONS RATHER THAN COPIED. A second copy of a
	 * file-local helper in this module is how a name collided with a
	 * neighbouring translation unit and reached `development` despite a full
	 * build, because Unreal's unity build leaves modified files out.
	 */
	bool InventoryPressTarget(FVector2D& OutPoint,
							  ACataclysmPlayerCharacter*& OutWearer) const;

	/**
	 * Says what a press did, when it did not do what was asked.
	 *
	 * LOGGED RATHER THAN SHOWN, and that is a gap rather than a decision. A
	 * press that changes nothing looks exactly like a press that missed.
	 * There is nowhere on the screen to say so yet; issue #831 records it.
	 */
	void ReportInventoryPress(ECataclysmWearResult Result) const;

	/**
	 * Which key fires the ability in a slot right now.
	 *
	 * PUBLIC BECAUSE THE SKILL BAR HAS TO LABEL ITS BOXES, and because the answer
	 * is not a constant anybody could write into the interface instead. The
	 * Support slot is on **W** under mouse movement and on **1** under keyboard
	 * movement, because keyboard movement needs W for walking forward --
	 * `tools/generate_input_assets.py` builds the two mapping contexts and says
	 * so where it does it. A label written down anywhere else would be wrong for
	 * half of the players.
	 *
	 * THE FIRST KEY WHEN SEVERAL ARE BOUND. Nothing binds two keys to one slot
	 * today; if something does, a box has room for one label and the first is as
	 * good an answer as any.
	 *
	 * IT DOES NOT LOAD THE INPUT CONFIGURATION. The heads-up display calls this
	 * from its draw pass, and a synchronous asset load there would stall a frame.
	 * A configuration that is not loaded yet answers with no key, which draws an
	 * unlabelled box for the frames before input has been set up.
	 *
	 * @param SlotTag a Slot.* tag. Anything else answers no key.
	 * @return the key, or an invalid `FKey` when nothing is bound
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Input")
	FKey KeyForAbilitySlot(FGameplayTag SlotTag) const;

protected:
	/** Every binding in the game, as data. Path set in Config/DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Input")
	TSoftObjectPtr<UCataclysmInputConfig> InputConfig;

	/** The key-to-action mapping. Path set in Config/DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** How long the move button may be held and still count as a click, in seconds. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Input")
	float ShortPressThreshold = 0.3f;

private:
	UCataclysmAbilitySystemComponent* GetCataclysmAbilitySystem() const;

	//~ Ability input. Neither names an ability; both carry only a Slot.* tag.
	void Input_AbilitySlotPressed(FGameplayTag SlotTag);
	void Input_AbilitySlotReleased(FGameplayTag SlotTag);

	//~ Native input.
	void Input_Move(const FInputActionValue& Value);
	void Input_MoveToCursorStarted();
	void Input_MoveToCursorHeld();
	void Input_MoveToCursorReleased();
	void Input_StandStillStarted();
	void Input_StandStillReleased();

	/** Mouse wheel. Handed to the pawn, because the camera boom lives there. */
	void Input_Zoom(const FInputActionValue& Value);

	/**
	 * Opens and closes the carried inventory.
	 *
	 * THE CONTROLLER OWNS THE WIDGET, not ACataclysmHUD. It already owns the key
	 * that opens it and the mouse position that has to be tested against it, and
	 * the heads-up display is on its way out: issue #650 deletes it once the
	 * three things it still draws have moved too. Issue #735.
	 *
	 * THE WIDGET IS MADE ON THE FIRST PRESS AND KEPT. Building 48 cells is not
	 * work to repeat every time the screen is opened, and a widget removed from
	 * the viewport costs nothing while it is not there.
	 */
	void Input_ToggleInventory();

	/** Puts CachedDestination under the cursor. False if the cursor hit nothing. */
	bool UpdateCachedDestination();

	/**
	 * Whether the possessed pawn is stunned and may not act.
	 *
	 * SEPARATE FROM bStandStill, WHICH SUPPRESSES MOVEMENT ONLY. The design
	 * defines a stun as the target being unable to act at all, so this gates
	 * skills as well, and the player did not choose it.
	 */
	bool IsPawnStunned() const;

	/**
	 * The drop whose drawn name the cursor is over, or nullptr for none.
	 *
	 * ASKS THE HEADS-UP DISPLAY, because it drew the names and is the only thing
	 * that knows where they landed. A controller with no ACataclysmHUD, or no
	 * mouse at all, answers nullptr and every click stays a move order.
	 */
	ACataclysmDroppedItem* DropUnderCursor() const;

	/**
	 * Takes every crafting material lying near the character. Issue #851.
	 *
	 * RUN EVERY FRAME, beside UpdatePendingPickup, because the character walks
	 * and the drops do not: there is no moment to hook other than arriving
	 * somewhere, and arriving somewhere is what every frame is.
	 *
	 * A FULL BAG STOPS IT AND THAT IS NOT A FAILURE. UCataclysmDropPickup::
	 * TakeInto leaves the drop lying there when it will not fit, so a material
	 * that cannot be carried stays on the floor and is collected later if room
	 * appears. Nothing is destroyed and nothing is retried in a way that
	 * costs anything: the sweep runs anyway.
	 */
	void CollectMaterialsNearby();

	/**
	 * Takes a drop if the character can reach it.
	 *
	 * @return true when the item is now in the inventory and the drop is gone.
	 *         **False when the inventory was full**, which is not a failure and
	 *         not retried: the design says an item that will not fit stays on
	 *         the floor for the player to choose about.
	 */
	bool TakeDrop(ACataclysmDroppedItem* Drop);

	/**
	 * Takes the drop the player clicked, once walking has brought them near it.
	 *
	 * WHY A CLICK ALONE IS NOT ENOUGH. A name can be clicked from anywhere on
	 * screen and the character has to be within
	 * UCataclysmDropPickup::PickupRangeCm to reach it, so a click from further
	 * off is a walk followed by a pick-up. Every game in the genre does this.
	 *
	 * RUNS EVERY FRAME, from PostProcessInput, because arriving is not an event
	 * anything here is told about.
	 */
	void UpdatePendingPickup();

	/**
	 * The carried inventory screen, once it has been opened at least once.
	 *
	 * A UPROPERTY BECAUSE IT IS A UObject THIS OWNS. Without one nothing keeps
	 * the widget alive between the frame it is removed from the viewport and the
	 * next time the key is pressed, and it would be collected.
	 *
	 * WHETHER IT IS OPEN IS WHETHER IT IS IN THE VIEWPORT, which
	 * UUserWidget::IsInViewport answers. A separate flag here would be a second
	 * record of the same thing.
	 */
	UPROPERTY()
	TObjectPtr<UCataclysmInventoryWidget> InventoryScreen = nullptr;

	/** Where the last cursor hit landed, in world space. */
	FVector CachedDestination = FVector::ZeroVector;

	/**
	 * The drop the player clicked and is walking toward, if any.
	 *
	 * WEAK, because the drop can be destroyed by anything at any time and a
	 * raw pointer would outlive it. Cleared when it is taken, when it goes
	 * away, when the player orders a move somewhere else, and on a stun.
	 */
	TWeakObjectPtr<ACataclysmDroppedItem> PendingPickup;

	/** How long the move button has been held this press, in seconds. */
	float FollowTime = 0.0f;

	/**
	 * True when this press of the move button started on an open screen.
	 *
	 * DECIDED ONCE AT THE PRESS AND REMEMBERED, rather than tested again on
	 * every frame of the hold. A player who presses on a grid cell and drags the
	 * cursor off the panel has not ordered a move, and testing each frame would
	 * start steering the character the moment the cursor left the edge.
	 *
	 * Cleared on release, and on the cancel that a mapping context change
	 * produces, for the same reason FollowTime is.
	 */
	bool bPressBeganOnInterface = false;

	/**
	 * True while the stand-still modifier is held. Suppresses movement only.
	 *
	 * Shift means stand still rather than force move. Last Epoch shipped the
	 * opposite and it is the subject of a standing player complaint; Path of
	 * Exile 2 uses shift for attack-in-place. Recorded in docs/DECISIONS.md.
	 */
	bool bStandStill = false;
};
