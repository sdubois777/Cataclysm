// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CataclysmInputConfig.generated.h"

class UInputAction;

/**
 * One input action and the ability slot it triggers.
 *
 * The tag is a Slot.* tag from the generated vocabulary. It never names an
 * ability, which is the whole point: the right mouse button means "whatever is
 * in the Heavy slot", and changing what is in that slot is a data change.
 */
USTRUCT(BlueprintType)
struct FCataclysmAbilityInputAction
{
	GENERATED_BODY()

	// EditAnywhere rather than EditDefaultsOnly throughout this file. A data
	// asset is an instance, not a class default, so EditDefaultsOnly marks the
	// property as not editable on instances and the editor scripting layer
	// refuses to write it -- which is how tools/generate_input_assets.py fills
	// this asset in.
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input", meta = (Categories = "Slot"))
	FGameplayTag SlotTag;
};

/**
 * One input action the pawn or controller handles itself, found by name.
 *
 * Movement, looking and the stand-still modifier are not abilities and have no
 * slot, so they are bound to specific C++ functions rather than routed through
 * the ability system. They still live in this asset so that every binding in the
 * game is described in one place.
 */
USTRUCT(BlueprintType)
struct FCataclysmNativeInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	/** Matched against the names in CataclysmInputActionNames. */
	UPROPERTY(EditAnywhere, Category = "Input")
	FName Name;
};

/** The native action names the player controller binds. Kept here so the
 *  generator and the controller cannot disagree about the spelling. */
namespace CataclysmInputActionNames
{
	/** Directional movement. A two-axis value; WASD and the left stick. */
	inline const FName Move = FName(TEXT("Move"));

	/** Move to the point under the cursor. Held, not tapped. */
	inline const FName MoveToCursor = FName(TEXT("MoveToCursor"));

	/** Held: cancel movement and stay put. Shift by default. */
	inline const FName StandStill = FName(TEXT("StandStill"));

	/** Camera distance. A one-axis value; the mouse wheel. Positive zooms in. */
	inline const FName Zoom = FName(TEXT("Zoom"));

	/**
	 * Open and close the carried inventory. Tapped, not held.
	 *
	 * ONE ACTION FOR BOTH DIRECTIONS, because it is a screen rather than a
	 * modifier. I by default, which is where Diablo, Path of Exile and Last
	 * Epoch all put it. Issue #731.
	 */
	inline const FName ToggleInventory = FName(TEXT("ToggleInventory"));
}

/**
 * Every input binding in the game, as data.
 *
 * This is what satisfies the requirement in issue #16 that slot-to-input mapping
 * be data-driven and not hard-coded per ability. Nothing in C++ names an
 * ability; the chain is
 *
 *     key -> UInputAction -> Slot.* tag -> whichever granted ability carries it
 *
 * and only the last step changes when the player equips a different weapon.
 */
// Not Const, unlike UCataclysmAbilitySet. A Const class refuses property writes
// from the editor scripting layer, and this asset is produced by
// tools/generate_input_assets.py rather than typed in by hand.
UCLASS(BlueprintType)
class CATACLYSM_API UCataclysmInputConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The action for a native binding, or null if this config does not list it. */
	const UInputAction* FindNativeAction(const FName& Name) const;

	/** The seven ability bindings. */
	const TArray<FCataclysmAbilityInputAction>& GetAbilityActions() const { return AbilityInputActions; }

	/** The native bindings: movement, move-to-cursor, stand still. */
	const TArray<FCataclysmNativeInputAction>& GetNativeActions() const { return NativeInputActions; }

protected:
	UPROPERTY(EditAnywhere, Category = "Input|Abilities", meta = (TitleProperty = "SlotTag"))
	TArray<FCataclysmAbilityInputAction> AbilityInputActions;

	UPROPERTY(EditAnywhere, Category = "Input|Native", meta = (TitleProperty = "Name"))
	TArray<FCataclysmNativeInputAction> NativeInputActions;
};
