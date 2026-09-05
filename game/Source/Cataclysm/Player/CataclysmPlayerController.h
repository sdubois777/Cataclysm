// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "CataclysmPlayerController.generated.h"

class ACataclysmDroppedItem;
class ACataclysmPlayerCharacter;
class UCataclysmAbilitySystemComponent;
class UCataclysmCharacterCreationWidget;
class UCataclysmCharacterSheetWidget;
class UCataclysmCityScreenWidget;
class UCataclysmEmpireMapWidget;
class UCataclysmInputConfig;
class UCataclysmInventoryWidget;
class UCataclysmPassiveTreeWidget;
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

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnUnPossess() override;

	/**
	 * Put the game into the one input mode it plays in.
	 *
	 * WHY THIS EXISTS RATHER THAN A CALL PER SCREEN. Issue #1015. Nothing in this
	 * project set an input mode at all: the game booted into whatever the engine
	 * defaults to, and the first screen to open replaced it. Each screen then
	 * built its own mode by hand -- GameAndUI to open, GameOnly to close -- so
	 * closing a screen left the game in `FInputModeGameOnly`, a mode it had never
	 * otherwise run in, and the player could no longer move. Reopening the screen
	 * restored GameAndUI and movement came back, which is what the project owner
	 * observed in a play test on 2026-08-26 and what makes this diagnosable at
	 * all.
	 *
	 * GameAndUI IS THE MODE, AND IT IS NOT A COMPROMISE. This game shows its
	 * cursor permanently -- the constructor sets `bShowMouseCursor` and nothing
	 * turns it off -- and every order a player gives is read off that cursor.
	 * `UpdateCachedDestination` traces under it, `CursorIsOverInterface` and
	 * `InventoryPressTarget` read its position. `FInputModeGameOnly` captures the
	 * mouse permanently and offers no way to say "do not hide or lock the cursor",
	 * which the two options below exist to say.
	 *
	 * CALL THIS, NEVER `SetInputMode` DIRECTLY. One mode in one place is what
	 * stops the next screen reintroducing the same fault, and
	 * `test_one_input_mode.py` fails if a second one appears in the source.
	 */
	void ApplyPlayingInputMode();

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
	 * Says that a press on the inventory screen did nothing at all, and why.
	 *
	 * THE GAP THIS FILLS. `ReportInventoryPress` above only speaks once a press
	 * has REACHED `UCataclysmWearing` and been refused. Two earlier ways to do
	 * nothing were both silent: a press that could not read a cursor position,
	 * and a press whose position landed in no cell. From a player's side those
	 * two are identical to each other and to a press that was never delivered,
	 * which is why issue #1016 -- "equipping often does nothing on the first
	 * right click" -- could not be turned into a cause by reading the code.
	 *
	 * WHAT IT PRINTS IS CHOSEN TO SEPARATE THE REMAINING CAUSES. The point
	 * itself, and whether the panel test accepted a point the cell test refused.
	 * A press the panel accepts and no cell does means the two hit tests
	 * disagree; a press neither accepts means the cursor is not where the press
	 * thinks it is.
	 *
	 * @param Button       "left" or "right", for the log line
	 * @param Point        the cursor in viewport pixels, zero if none was read
	 * @param bHadTarget   whether a cursor position and a pawn were found at all
	 */
	void ReportPressThatFoundNothing(const TCHAR* Button, const FVector2D& Point,
									 bool bHadTarget) const;

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

	/**
	 * Opens and closes the character creator.
	 *
	 * THE SAME SHAPE AS THE INVENTORY SCREEN, with one difference that matters:
	 * this screen is a Widget Blueprint rather than a C++ class that builds its
	 * own tree, so it is created from `CharacterCreationScreenClass` below
	 * rather than from a `StaticClass()`. A C++ widget whose whole layout lives
	 * in `BindWidget` properties has no tree of its own and would draw nothing
	 * at all. `docs/DECISIONS.md`, 2026-08-24. Issue #50.
	 *
	 * PUBLIC BECAUSE THE C KEY IS NOT THE ONLY WAY IN, and cannot be until the
	 * input assets have been generated in the editor. `Cataclysm.CharacterCreation`
	 * calls this, so a checkout whose `IMC_MouseMovement` predates the key can
	 * still open the screen and say what to run.
	 */
	void ToggleCharacterCreation();

	/**
	 * Opens and closes the passive class tree.
	 *
	 * THE SAME SHAPE AS THE CHARACTER CREATOR ABOVE, and public for the same
	 * reason: `Cataclysm.PassiveTree` calls it, so a checkout whose input assets
	 * predate the P key can still open the screen. Issue #50.
	 */
	void TogglePassiveTree();

	/**
	 * Opens and closes the empire overview.
	 *
	 * THE SAME SHAPE AS THE TWO ABOVE, and public for the same reason:
	 * `Cataclysm.EmpireMap` calls it. It has no key of its own, and that is not
	 * an oversight: the input assets are generated in the editor and adding a
	 * binding to them is a separate change from building a screen. Issue #1087.
	 */
	void ToggleEmpireMap();

	/**
	 * Opens and closes the character sheet.
	 *
	 * THE SAME SHAPE AS THE THREE ABOVE, and public for the same reason:
	 * `Cataclysm.CharacterSheet` calls it. It has no key of its own yet, for the
	 * reason the empire overview has none. Issues #1233 and #50.
	 */
	void ToggleCharacterSheet();

	/**
	 * Opens the city screen on one city, or closes it if it is already showing
	 * that city.
	 *
	 * IT TAKES A CITY, WHICH THE OTHER FOUR SCREENS DO NOT. There is one
	 * character sheet and one empire overview; there are 25 cities, and which
	 * one is being looked at is the whole content of the screen.
	 *
	 * OPENING IT ON A DIFFERENT CITY WHILE IT IS ALREADY OPEN SWITCHES CITIES
	 * rather than closing it. Clicking a second city on the empire overview
	 * means "show me that one", never "close this".
	 *
	 * `Cataclysm.CityScreen` calls it, and so does clicking a city on the empire
	 * overview. It has no key of its own, for the reason the empire overview has
	 * none. Issue #42.
	 */
	void ToggleCityScreen(int32 CityId);

	/**
	 * Scale the passive tree view, or fit the whole tree when given nothing.
	 *
	 * THE MOUSE WHEEL AND A DRAG ARE HOW THIS IS MEANT TO BE DRIVEN, and this
	 * exists beside them for the reason every other console command in this
	 * project exists: a person checking one thing quickly should not have to
	 * find the right number of wheel notches, and neither an automation test
	 * nor a script has a wheel.
	 *
	 * @param Notches  positive scales in, negative out, and zero fits the tree
	 */
	void ZoomPassiveTree(float Notches);

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

	/** The key's half of it. Calls ToggleCharacterCreation below. */
	void Input_ToggleCharacterCreation();

	/** The key's half of it. Calls TogglePassiveTree above. */
	void Input_TogglePassiveTree();

	/** Puts CachedDestination under the cursor. False if the cursor hit nothing. */
	bool UpdateCachedDestination();

	/**
	 * Whether the possessed pawn is under a hard stop and may not act.
	 *
	 * SEPARATE FROM bStandStill, WHICH SUPPRESSES MOVEMENT ONLY. The design
	 * defines a stun as the target being unable to act at all, so this gates
	 * skills as well, and the player did not choose it.
	 *
	 * IT ANSWERS FOR A KNOCKDOWN TOO, which section VI of the design document
	 * puts in the same row of its table as a stun. The name is unchanged because
	 * a stun is still the only one of the two that anything can apply to a
	 * player; see the .cpp.
	 */
	bool IsPawnStunned() const;

	/**
	 * Whether the possessed pawn is pinned and may not travel.
	 *
	 * NOT A HARD STOP AND DELIBERATELY NOT PART OF THE ONE ABOVE. A pinned
	 * character casts, swings and picks up as normal; the one thing it cannot do
	 * is walk. `UCataclysmSkillEffects::ApplyPin` carries the reasoning and issue
	 * #1149 carries the open question behind it.
	 */
	bool IsPawnPinned() const;

	/**
	 * Whether the possessed pawn may not travel, for either reason.
	 *
	 * WHAT EVERY MOVEMENT GATE ASKS. Three input handlers and the per-frame
	 * suppression read this; the one gate that refuses SKILLS reads
	 * `IsPawnStunned` instead, because a pin does not take a skill away.
	 */
	bool PawnCannotWalk() const;

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

	// ----------------------------------------------------------------------
	// The basic attack, which is on this button since issue #1187
	// ----------------------------------------------------------------------

	/**
	 * The enemy the cursor is over, or null.
	 *
	 * TRACED ON THE PAWN CHANNEL rather than on visibility, which is what
	 * `UpdateCachedDestination` uses to place a move order. Visibility stops at
	 * the floor and would answer with a piece of ground under the creature.
	 *
	 * IT REFUSES AN ALLY AND A CORPSE, not only a non-actor, so that pointing at
	 * either is a click on the world rather than a click on a target.
	 */
	AActor* EnemyUnderCursor() const;

	/**
	 * Swing at this target if it is close enough and the weapon is ready.
	 *
	 * @return whether a swing was actually started. False is the ordinary answer
	 *         while walking toward something or between swings, not an error.
	 */
	bool TrySwingAt(AActor* Target);

	/**
	 * Walk toward the enemy the player clicked, and swing on arrival.
	 *
	 * THE SAME SHAPE AS `UpdatePendingPickup` AND FOR THE SAME REASON: a target
	 * can be clicked from anywhere on screen, the character has to be within the
	 * basic attack's reach to hit it, and arriving is not an event anything here
	 * is told about. Runs every frame from `PostProcessInput`.
	 */
	void UpdatePendingAttack();

	/**
	 * Whether this button moves the character as well as attacking with it.
	 *
	 * TRUE UNDER MOUSE MOVEMENT AND FALSE UNDER KEYBOARD MOVEMENT, and read off
	 * the active mapping context rather than stored as a second setting that
	 * could disagree with it. The question asked is the real one: **does
	 * something other than this button already move the character?** If a key
	 * is bound to directional movement then the answer is yes and this button
	 * only attacks and picks things up.
	 *
	 * GAMEPAD BINDINGS DO NOT COUNT. Directional movement is on the left stick
	 * in both schemes, so counting it would make both schemes look like keyboard
	 * movement and the left mouse button would stop moving anybody.
	 *
	 * NO CONTEXT MEANS TRUE, which is how this behaved before either scheme
	 * existed, so a failure to load an asset does not silently take movement
	 * away.
	 */
	bool LeftButtonAlsoMoves() const;

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

	/**
	 * Which Widget Blueprint the character creator is.
	 *
	 * A SOFT PATH RESOLVED ON THE FIRST PRESS, rather than a hard reference
	 * found in the constructor. The asset is generated by
	 * `tools/generate_interface_assets.py`, so a checkout that has not run it
	 * does not have one -- and a hard reference to a missing class logs a
	 * warning on every single start of the editor, about a screen nobody has
	 * pressed. This way the complaint arrives when the key is pressed and says
	 * what to run.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmCharacterCreationWidget> CharacterCreationScreenClass =
		TSoftClassPtr<UCataclysmCharacterCreationWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_CharacterCreation.WBP_CharacterCreation_C")));

	/** The character creator, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<UCataclysmCharacterCreationWidget> CharacterCreationScreen = nullptr;

	/** Which Widget Blueprint the passive tree screen is. Soft, for the reason
	 *  the character creator's is. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmPassiveTreeWidget> PassiveTreeScreenClass =
		TSoftClassPtr<UCataclysmPassiveTreeWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_PassiveTree.WBP_PassiveTree_C")));

	/** The passive tree screen, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<UCataclysmPassiveTreeWidget> PassiveTreeScreen = nullptr;

	/** Which Widget Blueprint the empire overview is. Soft, for the reason
	 *  the character creator's is. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmEmpireMapWidget> EmpireMapScreenClass =
		TSoftClassPtr<UCataclysmEmpireMapWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_EmpireMap.WBP_EmpireMap_C")));

	/** The empire overview, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<UCataclysmEmpireMapWidget> EmpireMapScreen = nullptr;

	/** Which Widget Blueprint the character sheet is. Soft, for the reason
	 *  the character creator's is. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmCharacterSheetWidget> CharacterSheetScreenClass =
		TSoftClassPtr<UCataclysmCharacterSheetWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_CharacterSheet.WBP_CharacterSheet_C")));

	/** The character sheet, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<UCataclysmCharacterSheetWidget> CharacterSheetScreen = nullptr;

	/** Which Widget Blueprint one city's screen is. Soft, for the reason
	 *  the character creator's is. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmCityScreenWidget> CityScreenClass =
		TSoftClassPtr<UCataclysmCityScreenWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_CityScreen.WBP_CityScreen_C")));

	/**
	 * The city screen, once it has been opened at least once.
	 *
	 * ONE WIDGET FOR ALL 25 CITIES, told which one to show. A widget per city
	 * would be 25 of them holding the same rows.
	 */
	UPROPERTY()
	TObjectPtr<UCataclysmCityScreenWidget> CityScreen = nullptr;

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

	/**
	 * The enemy the player clicked and is walking toward, if any.
	 *
	 * WEAK, for the reason `PendingPickup` above is: a creature can be killed by
	 * anything at any time. Cleared when it is reached, when it dies, when the
	 * player orders a move somewhere else, and on a movement key press.
	 */
	TWeakObjectPtr<AActor> PendingAttack;

	/**
	 * When the last basic attack swing was started, in world seconds.
	 *
	 * FAR ENOUGH IN THE PAST THAT THE FIRST SWING IS ALLOWED. A character that
	 * has never swung must not have to wait one interval before its first hit,
	 * and a large negative number says that without a separate "has ever swung"
	 * flag for the two to disagree about.
	 */
	float LastSwingSeconds = -1000.0f;

	/**
	 * True when this press of the button started on an enemy.
	 *
	 * DECIDED AT THE PRESS AND REMEMBERED, the same as `bPressBeganOnInterface`
	 * below and for the same reason: a player who presses on a creature and
	 * drags the cursor off it has ordered an attack, not a walk, and asking
	 * again each frame would turn the attack into a move the moment the creature
	 * stepped aside.
	 */
	bool bPressBeganOnAnEnemy = false;

	/**
	 * True when a movement key moved the character this frame.
	 *
	 * SET BY `Input_Move` AND CLEARED AT THE END OF `PostProcessInput`, so it
	 * always describes the frame being processed. The engine runs that function
	 * after the frame's input, which is what makes the ordering work.
	 *
	 * WHAT IT IS FOR. `UpdatePendingAttack` walks the character toward a target
	 * that is out of reach and stops the walk when it arrives. Both of those
	 * fight the player's own keys: a path issued while they are steering is
	 * cancelled by `Input_Move` on the next frame and re-issued on the one after,
	 * and a `StopMovement` on arrival would cancel a walk the player is giving
	 * rather than one this code started. Reading this makes the player's keys win
	 * instead of whichever ran last.
	 */
	bool bSteeredByAKeyThisFrame = false;

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
	 * Ability slots whose current press has already acted on the inventory.
	 *
	 * THE SAME IDEA AS `bPressBeganOnInterface` ABOVE, FOR THE OTHER BUTTON, and
	 * it exists because the two buttons are bound to different trigger events.
	 * The move button reports Started once; `BindAbilityActions` binds an
	 * ability slot to `ETriggerEvent::Triggered`, which fires EVERY FRAME the
	 * button is held. That is right for an ability and wrong for a click.
	 *
	 * WHAT IT COST BEFORE IT EXISTED. Issue #1016: a right press on a carried
	 * item wore it, and the second frame of the same press wore it back off
	 * again. `UCataclysmWearing::WearFromCarried` empties the cell and returns
	 * whatever came off the body to the first free slot, which is usually that
	 * same cell, so each frame swapped the two items over. Whether the player
	 * ended up wearing what they clicked depended on whether they held the
	 * button for an odd or an even number of frames, and the project owner
	 * reported it as "it will often not work on the first try".
	 *
	 * A SET RATHER THAN A BOOL, because two ability slots can be held at once
	 * and only one of them may be over the screen.
	 */
	TSet<FGameplayTag> SlotsAlreadyPressedOnTheInventory;

	/**
	 * True while the stand-still modifier is held. Suppresses movement only.
	 *
	 * Shift means stand still rather than force move. Last Epoch shipped the
	 * opposite and it is the subject of a standing player complaint; Path of
	 * Exile 2 uses shift for attack-in-place. Recorded in docs/DECISIONS.md.
	 */
	bool bStandStill = false;
};
