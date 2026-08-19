// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "CataclysmPlayerController.generated.h"

class ACataclysmDroppedItem;
class UCataclysmAbilitySystemComponent;
class UCataclysmInputConfig;
class UInputMappingContext;
struct FInputActionValue;

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
	 * True while the stand-still modifier is held. Suppresses movement only.
	 *
	 * Shift means stand still rather than force move. Last Epoch shipped the
	 * opposite and it is the subject of a standing player complaint; Path of
	 * Exile 2 uses shift for attack-in-place. Recorded in docs/DECISIONS.md.
	 */
	bool bStandStill = false;
};
