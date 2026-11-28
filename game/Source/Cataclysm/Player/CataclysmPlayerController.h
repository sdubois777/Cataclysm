// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "CataclysmPlayerController.generated.h"

enum class ECataclysmAbilitySlot : uint8;

class ACataclysmDroppedItem;
class ACataclysmPlayerCharacter;
class UCataclysmAbilitySystemComponent;
class UCataclysmCharacterCreationWidget;
class UCataclysmCharacterSheetWidget;
class UCataclysmCityScreenWidget;
class UCataclysmEmpireMapWidget;
class UCataclysmEmpireTreeWidget;
class UCataclysmForgeWidget;
class UCataclysmFloorModifierPanel;
class UCataclysmGroundItemTooltip;
class ACataclysmComparisonStandIn;

/**
 * What `ACataclysmPlayerController::ShowFloorModifiers` was last told, and how many times it has been told anything,
 * for a test that cannot read the screen. Issue #1841.
 *
 * ONLY THE LAST CALL, so it never grows during play (ruled 2026-11-01). Set before the local-player check, because a
 * test world has no local player and the panel is never made there.
 */
struct FCataclysmFloorModifiersShown
{
	TArray<FName> RowKeys;
	int32 FloorNumber = 0;
	TMap<FName, FString> LiveCounts;
	FString ScoreLine;
	int32 Calls = 0;
};
class UCataclysmInputConfig;
class UCataclysmInventoryWidget;
class UCataclysmPassiveTreeWidget;
class UCataclysmPauseScreenWidget;
class UInputMappingContext;
struct FInputActionValue;

/**
 * What one press of Esc did. The project owner, 2026-11-24: "Offline and online, pause is a must".
 * `ACataclysmPlayerController::EscapeDoes` decides the first three from two facts; `Refused` is the engine's answer
 * when the game could not be paused. `docs/DECISIONS.md`, 2026-11-24.
 */
enum class ECataclysmEscape : uint8
{
	/** Screens were open: every one Esc closes was closed, and the game was not paused. */
	ClosedScreens,

	/** Nothing was open: the game is paused and the pause screen asked for. */
	Paused,

	/** The game was paused: it runs again and the pause screen is gone. */
	Resumed,

	/** Nothing was open and the game could not be paused: no game mode to ask, on a client, and the log says so. */
	Refused,
};

// THE UNDERLYING TYPE IS PART OF THE DECLARATION for a scoped enum, and it
// has to match CataclysmWearing.h exactly or the two are different types.
// Declared rather than included, because the header is only needed for one
// parameter and including it here would pull the item system into every
// translation unit that knows about the controller.
enum class ECataclysmWearResult : uint8;

// AND THE GEAR SLOT, for the same reason and with the same rule: its underlying
// type matches CataclysmEquipmentComponent.h. AskBeforeWearing's slot, issue
// #837's slice 3.
enum class ECataclysmGearSlot : uint8;

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
	 * Shows the dungeon modifiers in force on the floor being walked, or hides
	 * the panel when there are none. Issue #41.
	 *
	 * CALLED BY THE DUNGEON GAME MODE WHENEVER A FLOOR BEGINS, not opened by a
	 * key. A modifier changes the floor the player is standing on, so what it
	 * says has to be on the screen while that floor is played rather than behind
	 * a menu. The panel never takes a click, so it cannot get in the way of
	 * click-to-move.
	 *
	 * CALLED AGAIN WHENEVER A COUNT MOVES, since 2026-09-14, and not only when a
	 * floor begins. A count shown once at floor entry is always the count the
	 * player had before they did anything, which is nothing.
	 *
	 * @param RowKeys     `FCataclysmFloorBrief::Modifiers`. Empty hides the panel
	 * @param FloorNumber counted from 1
	 * @param LiveCounts  what each row is counting now, by row key. Defaulted
	 *                    empty; see `FCataclysmFloorModifierLine::LiveCount`
	 */
	void ShowFloorModifiers(const TArray<FName>& RowKeys, int32 FloorNumber,
							const TMap<FName, FString>& LiveCounts =
								TMap<FName, FString>(),
							const FString& ScoreLine = FString());

	/** What `ShowFloorModifiers` was last told, for a test that cannot read the screen. Issue #1841. */
	const FCataclysmFloorModifiersShown& FloorModifiersShown() const { return LastFloorModifiersShown; }

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
	 * THE ONE PLACE THE GAME SETS AN INPUT MODE, for this controller or any other: the playing mode above, and for a
	 * menu the same mode with the menu focused. `ApplyPlayingInputMode` calls it for this controller; the character
	 * select screen's controller, which is not one of these, calls it for its menu. Issue #1015's one mode in one
	 * place, kept; select-screen step 3a (#753), ruled 2026-10-31.
	 *
	 * IT SAYS WHICH IT ASKED FOR, "Playing" or "Menu", in `OutAskedFor` when given, for a test to read: the engine applies a mode only to a
	 * controller with a game viewport and a local player, and records its name only then, and a test world has neither.
	 * Whether the mode is right is still for a person playing to see.
	 */
	static void ApplyPlayingInputModeTo(APlayerController& Controller, class UUserWidget* MenuToFocus = nullptr,
										FString* OutAskedFor = nullptr);

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
	 * Shows the map of the current floor if it is hidden, and hides it if it is shown. Issue #2805.
	 *
	 * THE GAME KEEPS RUNNING: the map is drawn over play, as Diablo II's Automap is, and takes no input. On a level with
	 * no dungeon floor it is still toggled, and draws nothing until there is one; the log says so.
	 */
	void ToggleFloorMap();

	/** Whether the player has the floor map shown. `ACataclysmHUD::DrawFloorMap` reads it. */
	bool IsFloorMapShown() const { return bFloorMapShown; }

	/**
	 * Whether a full screen is open over the game: the inventory, the character sheet or its creator, the passive
	 * tree, the skill assignment, or one of the empire's screens. The floor map is not drawn under one. Issue #2805.
	 */
	bool IsAFullScreenOpen() const;

	/**
	 * Esc, READ HERE BEFORE ENHANCED INPUT SEES IT, and every other key passed on unchanged. The project owner,
	 * 2026-11-24: "Offline and online, pause is a must". A press runs `PressEscape`; a release or a repeat does
	 * nothing, so holding Esc does not pause and resume over and over.
	 *
	 * NOT AN INPUT ACTION, and that is a judgement with two reasons. The engine calls this from the game viewport
	 * whether or not the world is paused, so Esc reaches it while paused with nothing more to set; an input action
	 * would need `bTriggerWhenPaused` set by `tools/generate_input_assets.py`, which only an editor run can write. And
	 * Esc is the one key a game menu needs that a player should not rebind away. `docs/DECISIONS.md`, 2026-11-24.
	 *
	 * IN PLAY IN EDITOR THE EDITOR TAKES ESC FIRST, for its Stop command, before this is called
	 * (`UGameViewportClient::InputKey` asks the editor's play-world commands before the player controller).
	 * `game/Config/DefaultEditorSettings.ini` moves Stop to Shift+Esc for this project, so Esc reaches the game there.
	 */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	/**
	 * What a press of Esc does, as `InputKey` runs it: what `EscapeDoes` decides from whether a screen is open and
	 * whether the game is paused, done. Public so a test can press it; a test world has no viewport to press from.
	 */
	ECataclysmEscape PressEscape();

	/**
	 * What Esc does, from two facts. A PAUSED GAME RESUMES, because the pause screen is the one on top. Otherwise AN
	 * OPEN SCREEN CLOSES and the game goes on. Otherwise THE GAME PAUSES. Never `Refused`, which is the engine's answer
	 * and not this rule's. Static, so a test can ask the rule with no screen.
	 */
	static ECataclysmEscape EscapeDoes(bool bAScreenIsOpen, bool bGameIsPaused);

	/**
	 * Pauses the game and puts the pause screen up. Through `APlayerController::SetPause`, the engine's own pause:
	 * the world stops ticking its actors and its timers and its clock stands still (`UWorld::Tick`), so combat,
	 * creature brains, cooldowns, damage over time, ground zones, projectiles and every dungeon timer stop together,
	 * and Slate, which draws and clicks the screen, does not stop. True when the game is paused afterwards; false,
	 * logged, when the game mode refused: none to ask, which a client has.
	 */
	bool PauseTheGame();

	/** Takes the pause screen down and lets the game run. True when the game runs afterwards. */
	bool ResumeTheGame();

	/**
	 * Whether the pause screen is wanted: from `PauseTheGame` until `ResumeTheGame`. Set before the screen is made, as
	 * `ShowFloorModifiers` records what it was told, because a test world has no local player and so no screen.
	 */
	bool IsPauseScreenWanted() const { return bPauseScreenWanted; }

	/** Whether Esc has a screen to close: any full screen (`IsAFullScreenOpen`) or the choice panel. */
	bool HasAScreenEscCloses() const;

	/**
	 * Pauses a walk that has just been resumed from a save, until the player presses a key or a button. Issue #2970.
	 * The project owner, 2026-11-27, verbatim: "it will also need to freeze all of your minions until you press a
	 * button as well. Maybe just make them load in paused?"
	 *
	 * THE GAME'S OWN PAUSE, the one `PauseTheGame` uses (`APlayerController::SetPause`), so creatures, the player's
	 * minions, cooldowns, damage over time and every dungeon timer stop together, with no rule of their own for each.
	 * NO PAUSE SCREEN: the heads-up display draws `HeldUntilFirstInputLine` instead, while `IsHeldUntilFirstInput`.
	 *
	 * THE HOLD IS RECORDED BEFORE THE PAUSE IS ASKED FOR, as `PauseTheGame` records the pause screen: a world with no
	 * game mode to ask (a client, or a test world) refuses the pause, and the line and the first press still work.
	 * True when the game is paused afterwards. Called by `FCataclysmSaveLoad::CharacterAndRun` when it resumes a walk.
	 */
	bool HoldUntilFirstInput();

	/** Whether a resumed walk is waiting for the player's first press: from `HoldUntilFirstInput` until that press. */
	bool IsHeldUntilFirstInput() const { return bHeldUntilFirstInput; }

	/**
	 * Ends the hold and lets the game run, unless the pause screen is up. `InputKey` calls it on the first press. True
	 * when the game runs afterwards.
	 */
	bool ReleaseTheHold();

	/**
	 * Whether a key event ends the hold. A PRESS OF ANY KEY OR BUTTON: keyboard, mouse button or gamepad button, Esc
	 * included. Not a release or a repeat, not the mouse wheel (a scroll is not a press), and not an analogue axis, whose
	 * events are not presses. Static, so a test can ask the rule with no controller.
	 */
	static bool PressEndsTheHold(const FKey& Key, EInputEvent Event);

	/** The line the heads-up display draws while the walk is held. */
	static const TCHAR* HeldUntilFirstInputLine() { return TEXT("Press any key or button to resume"); }

	/**
	 * Closes every screen Esc closes, EACH THROUGH WHAT CLOSES IT TODAY: its own toggle, which on an open screen is its
	 * close, so Esc does exactly what the screen's key does (the inventory lets go of what is on the cursor); the
	 * empire's four through `CloseEmpireScreens`, as a walk closes them; and the choice panel through `Close`, which is
	 * its "Leave" and chooses nothing. The floor map is not a screen: it is drawn over play and stays.
	 */
	void CloseEveryScreenEscCloses();

	/**
	 * For automation tests: a screen Esc closes, standing in for the real ones, which a test world cannot put in a
	 * viewport. `CloseEveryScreenEscCloses` clears it. Nothing in the game calls it.
	 */
	void SetAScreenOpenForTest(bool bOpen) { bScreenOpenForTest = bOpen; }

	/**
	 * Marks what the player can see of the floor as seen, when the player has entered a new cell or the floor has
	 * been built again. Issue #2805. Runs every frame from `PostProcessInput`; no automation test ticks a player
	 * controller, so `tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py` pins the call.
	 */
	void RefreshFloorMapSeen();

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
	 * SHOWN ON THE SCREEN AND LOGGED, since issue #837's slice 3. Until then it
	 * was only logged, a gap issue #831 recorded: a press that changes nothing
	 * looked exactly like a press that missed. The owner's Q6 of 2026-11-24 had
	 * the last weapon's refusal shown, and every refusal is shown the same way,
	 * above the inventory panel (`UCataclysmInventoryWidget::ShowPressLine`), in
	 * the words `UCataclysmInventoryScreen::PressLineFor` gives. A press that did
	 * what was asked takes the line away.
	 */
	void ReportInventoryPress(ECataclysmWearResult Result) const;

	/**
	 * Asks, on the choice panel, before the item in this carried cell is put on past this tier's Consumption
	 * Threshold, and puts it on only for "Wear it". Issue #2687. The answer goes through
	 * `UCataclysmWearing::WearAfterConfirming`, so a cell that changed while the question was open puts nothing on.
	 *
	 * @param Into  the gear slot a held item was pressed onto (issue #837, slice 3): the question is about that slot
	 *              and the yes wears it there. `ECataclysmGearSlot::Count` for the right press, `Equip`'s choice.
	 */
	void AskBeforeWearing(class ACataclysmPlayerCharacter* Wearer, int32 CarriedSlot, ECataclysmGearSlot Into);

	/**
	 * Whether the question `AskBeforeWearing` opened is on the screen. While it is, a press on the inventory screen
	 * does nothing: the question names one carried cell, and a press that moved the item in it would leave the answer
	 * about a different one. Issue #2687.
	 */
	bool WearingQuestionIsOpen() const;

	/** The choice panel, created the first time it is needed; null, with the reason logged, when it cannot be. */
	class UCataclysmChoicePanelWidget* EnsureChoicePanel();

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
	 * It reads the configuration `HoldTheInputConfig` holds; before input has
	 * been set up that is none, which answers with no key and draws an
	 * unlabelled box.
	 *
	 * @param SlotTag a Slot.* tag. Anything else answers no key.
	 * @return the key, or an invalid `FKey` when nothing is bound
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Input")
	FKey KeyForAbilitySlot(FGameplayTag SlotTag) const;

	/**
	 * Load the input configuration and hold it for as long as this controller lives.
	 *
	 * HELD IN A PROPERTY BECAUSE NOTHING ELSE HOLDS IT. `InputConfig` is a soft pointer, which keeps nothing loaded, and
	 * until 2026-11-27 the loaded configuration was kept only in a local in `SetupInputComponent`. The editor happens
	 * to keep the asset loaded; a packaged game does not, so its first garbage collection, about a minute in, freed
	 * it. From then `KeyForAbilitySlot` answered no key, so a right click over the inventory was sent to the Heavy
	 * skill instead of equipping the item, and the skill bar lost its key letters.
	 *
	 * `SetupInputComponent` calls it; public so a test, whose world has no local player and so sets up no input, can.
	 *
	 * @return the configuration held, or null when none is set or it will not load
	 */
	const UCataclysmInputConfig* HoldTheInputConfig();

	/** The configuration `HoldTheInputConfig` holds, or null before it has run. */
	const UCataclysmInputConfig* GetHeldInputConfig() const { return HeldInputConfig; }

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
	 * PUBLIC BECAUSE `Cataclysm.CharacterCreation` CALLS IT, and since
	 * 2026-11-19 that console command is the only way to it inside a level. C
	 * opened it until the project owner gave C to the character sheet (issue
	 * #2770). New characters are made from the front end's New character
	 * screen, `UCataclysmNewCharacterWidget`, which is a different screen and
	 * does not come through here.
	 */
	void ToggleCharacterCreation();

	/**
	 * Opens and closes the passive class tree.
	 *
	 * THE SAME SHAPE AS THE CHARACTER CREATOR ABOVE, and public because
	 * `Cataclysm.PassiveTree` calls it, so a checkout whose input assets
	 * predate the P key can still open the screen. Issue #50.
	 */
	void TogglePassiveTree();

	/**
	 * Opens and closes the empire overview.
	 *
	 * THE SAME SHAPE AS THE TWO ABOVE, and public for the same reason:
	 * `Cataclysm.EmpireMap` calls it, so a checkout whose input assets predate
	 * the M key can still open the screen. Issue #1087.
	 *
	 * THE M KEY CALLS IT TOO, since 2026-11-17 (ruled by the coordinating
	 * session on 2026-11-03: "M opens and closes the empire map"; issue #2733
	 * slice 3), through `Input_ToggleEmpireMap`. The key and the command are one
	 * function, so they cannot behave differently.
	 *
	 * IT STARTS AN EMPIRE RUN WHEN THERE IS NONE, which the console command used
	 * to do itself before calling this. Moved here on 2026-11-17 so the key does
	 * what the command does without copying it: asking for the empire map is
	 * asking for an empire. A character's run, begun when it is made or loaded,
	 * is already there and is left alone.
	 */
	void ToggleEmpireMap();

	/**
	 * Opens the empire overview and leaves it open: refreshed and shown, and never closed by a second call, which
	 * `ToggleEmpireMap` would do. `ACataclysmCapitalGameMode` calls it when the player arrives in the capital. Issue
	 * #2733 slice 2.
	 *
	 * @return whether the screen is shown afterwards; false, logged, when its asset does not load.
	 */
	bool OpenEmpireMap();

	/**
	 * Opens and closes the character sheet.
	 *
	 * THE SAME SHAPE AS THE THREE ABOVE, and public for the same reason:
	 * `Cataclysm.CharacterSheet` calls it, so a checkout whose input assets
	 * predate the C key can still open the screen. Issues #1233 and #50.
	 *
	 * THE C KEY CALLS IT TOO, since 2026-11-19 (ruled by the project owner:
	 * "C opens the sheet"; issue #2770), through `Input_ToggleCharacterSheet`.
	 * The key and the command are one function, so they cannot behave
	 * differently. C opened the character creator until then.
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
	 * overview. It has no key of its own: a key would have to choose a city, and
	 * clicking one on the empire overview, which M opens since 2026-11-17, is
	 * how a city is chosen. Issue #42.
	 */
	void ToggleCityScreen(int32 CityId);

	/**
	 * Closes the empire overview and the city screen, whichever are open.
	 *
	 * WHAT A WALK BUTTON CALLS once a dungeon has been entered, so the player is
	 * left standing on the floor they walked into rather than behind two screens.
	 * Issue #2715. A screen that is not open is left alone, and the input mode is
	 * the one every screen's closing sets, for the reason issue #1015 recorded.
	 */
	void CloseEmpireScreens();

	/**
	 * Opens and closes the empire upgrade tree screen. Issue #2741 slice 2.
	 *
	 * THE EMPIRE MAP'S "Empire upgrade tree" BUTTON CALLS IT, and so do the screen's own Close button and
	 * `Cataclysm.EmpireTree`. It opens wherever the map opens -- during a walk too, to be read; buying, choosing and
	 * respec are refused there (`UCataclysmGameInstance::EmpireTreeChangeRefusal`). REFRESHED ON EVERY OPENING: points
	 * banked by a clear, a load, or another character of the mode can have moved the tree since it was last open.
	 */
	void ToggleEmpireTree();

	/** Whether the empire upgrade tree screen is open. For the empire map's button and for tests. */
	bool IsEmpireTreeOpen() const;

	/**
	 * Opens and closes the Cataclysmic Forge's screen. The owner's ruling of 2026-11-19, "build it next".
	 *
	 * THE EMPIRE MAP'S "Forge" BUTTON CALLS IT, until the capital's Smith exists, and so does the screen's own Close
	 * button. It opens wherever the map opens -- during a walk too, to be read; every craft is refused there
	 * (`UCataclysmForgeCraft::WalkRefusal`). REFRESHED ON EVERY OPENING: a walk changes what is carried and worn.
	 */
	void ToggleForge();

	/** Whether the forge's screen is open. For tests. */
	bool IsForgeOpen() const;

	/**
	 * Opens and closes the merchant's screen. Issue #48, slice S1, the owner's O5 of 2026-11-22: "Screen from the
	 * empire map". THE FORGE'S SHAPE: the empire map's "Merchant" button calls it, and so does the screen's own Close. It
	 * opens wherever the map opens, during a walk too, to be read; a sale is refused there
	 * (`UCataclysmMerchant::WalkRefusal`). REFRESHED ON EVERY OPENING: a walk changes what is carried.
	 */
	void ToggleMerchant();

	/** Whether the merchant's screen is open. For tests. */
	bool IsMerchantOpen() const;

	/**
	 * Open the choice panel for this floor object, or show its choices on the panel already open. Called when the
	 * player clicks the object's name in reach, or arrives at it after a click from further off. Issues #1820 and
	 * #41, the choice screen.
	 */
	void OpenChoicePanel(class ACataclysmFloorObject* Object);

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

	/**
	 * Opens and closes the skill-choice screen: which pool skill goes on which of the six keys. Issue #837 layer 4.
	 *
	 * THE SAME SHAPE AS THE PASSIVE TREE'S, and public for the same reason: `Cataclysm.ChooseSkills` calls it, so a
	 * checkout whose input assets predate the K key can still open the screen (ruled Q1: "K plus
	 * Cataclysm.ChooseSkills"). It does not pause the game and opens anywhere, a dungeon included (ruled Q2).
	 */
	void ToggleSkillAssignment();

	/**
	 * The key of the skill box on the player's bar under a point of the screen, or `None`. Issue #837 slice 2, the owner's
	 * Q3: a left press over a box opens the skill screen with that key selected and is neither a move nor a swing
	 * (`Input_MoveToCursorStarted` asks this first). The viewport is the caller's, so a test can ask without one.
	 */
	ECataclysmAbilitySlot SkillBoxKeyAt(const FVector2D& Point, const FVector2D& Viewport) const;

	/**
	 * Opens the skill screen if it is closed, never closing it, and selects `Key` when it is one of the six (the basic
	 * attack's box opens the screen with nothing selected). Issue #837 slice 2. Whether the screen is open after.
	 */
	bool OpenSkillAssignmentOn(ECataclysmAbilitySlot Key);

	/**
	 * Opens and closes the loot filter screen. Issue #2846, slice LF2. The skill-choice screen's shape, and public for
	 * its reason: `Cataclysm.EditLootFilters` calls it, so a checkout whose input assets predate the L key can still
	 * open the screen. It does not pause the game.
	 */
	void ToggleLootFilterScreen();

protected:
	/** Every binding in the game, as data. Path set in Config/DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Input")
	TSoftObjectPtr<UCataclysmInputConfig> InputConfig;

	/** The key-to-action mapping. Path set in Config/DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Cataclysm|Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** `InputConfig`, loaded and held so garbage collection cannot free it while it is read. See `HoldTheInputConfig`. */
	UPROPERTY(Transient)
	TObjectPtr<const UCataclysmInputConfig> HeldInputConfig = nullptr;

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

	/** Alt held and let go: show and stop showing the drops the loot filter hides. Issue #2846, slice LF1 (O3). */
	void Input_RevealDropsStarted();
	void Input_RevealDropsReleased();

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

	/** The C key's half of it. Calls ToggleCharacterSheet above, as `Cataclysm.CharacterSheet` does. Issue #2770. */
	void Input_ToggleCharacterSheet();

	/** The key's half of it. Calls TogglePassiveTree above. */
	void Input_TogglePassiveTree();

	/** The K key's half of it. Calls ToggleSkillAssignment above. Issue #837 layer 4. */
	void Input_ToggleSkillAssignment();

	/** The L key's half of it. Calls ToggleLootFilterScreen above. Issue #2846, slice LF2. */
	void Input_ToggleLootFilterScreen();

	/** The M key's half of it. Calls ToggleEmpireMap above, as `Cataclysm.EmpireMap` does. Ruled 2026-11-03. */
	void Input_ToggleEmpireMap();

	/** The Tab key's half of it. Calls ToggleFloorMap. Issue #2805. */
	void Input_ToggleFloorMap();

	/** See `IsFloorMapShown`. Hidden when the controller starts. */
	bool bFloorMapShown = false;

	/** The floor and cell the seen cells were last revealed from, and that floor's build then. See `RefreshFloorMapSeen`. */
	TWeakObjectPtr<class ACataclysmDungeonFloor> FloorMapLastFloor;
	FIntPoint FloorMapLastCell = FIntPoint(-1, -1);
	int32 FloorMapLastBuild = -1;

	/**
	 * The four potion keys, 2 to 5. One handler a slot, because a native
	 * action is bound by name with no argument. Each drinks through
	 * `UCataclysmPotions::Drink`, as `Cataclysm.DrinkPotion` does. Issue #806.
	 */
	void Input_DrinkPotion1();
	void Input_DrinkPotion2();
	void Input_DrinkPotion3();
	void Input_DrinkPotion4();

	/** Drink the potion in a slot, 0 to 3, and log a refusal. */
	void DrinkPotion(int32 Slot);

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

	/** The floor object whose name is under the cursor, or null, as `DropUnderCursor` finds a drop. */
	class ACataclysmFloorObject* FloorObjectUnderCursor() const;

	/**
	 * Shows, moves or hides the ground hover's tooltip for the drop under the cursor. Issue #2807: the bag's tooltip
	 * and comparison for an item on the ground, see `UCataclysmGroundItemHover`. Run every frame from
	 * `PostProcessInput`; the text is worked out again only when `UCataclysmGroundItemHover::NeedsRebuild` says so.
	 *
	 * NOTHING WHILE THE CURSOR IS OVER THE INTERFACE, the inventory screen included, as
	 * `ACataclysmHUD::CreatureUnderCursor` gives nothing then.
	 */
	void RefreshGroundItemHover();

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

public:
	/**
	 * Whether the key that shows hidden drops is held: Alt by default. Issue #2846, slice LF1. The owner's ruling O3 of
	 * 2026-11-22: a drop the loot filter hides stays on the floor, holding Alt shows it, and while shown it can be taken.
	 * `ACataclysmHUD::DrawDropNames` reads it every frame, so a name shown this way is drawn, clicked and hovered as any
	 * other while the key is down.
	 */
	bool IsRevealingHiddenDrops() const { return bRevealHiddenDrops; }

	/** Holds or lets go of the reveal key, exactly as the two key events do, for automation tests. */
	void SetRevealingHiddenDropsForTest(bool bHeld) { bRevealHiddenDrops = bHeld; }

	/**
	 * Whether an actor found under the cursor is an enemy the player may click: a living hostile that is not the
	 * player's own pawn and is not hidden. A creature the vision system hides cannot be clicked: "a label you cannot see
	 * is a label you cannot click". Static so a test can ask it. Issues #1820 and #41.
	 */
	static bool IsClickableEnemy(const AActor* Found, const AActor* ControlledPawn);

	/**
	 * The click's take and the per-frame sweep, exactly as play runs them, for automation
	 * tests. Issues #1820 and #41: Trick or Treat's "clicked pickups only" rests on
	 * `TakeDrop` saying "by hand" and the sweep not, and a test world has no input to click
	 * with. Nothing in the game calls these.
	 */
	bool TakeDropForTest(ACataclysmDroppedItem* Drop) { return TakeDrop(Drop); }
	void CollectMaterialsNearbyForTest() { CollectMaterialsNearby(); }

	/**
	 * The basic attack's swing exactly as play runs it, and when the last one
	 * started, for automation tests. Issue #1515: Follow Through's ruling that
	 * its repeat of a basic attack leaves the swing interval alone is a claim
	 * about `LastSwingSeconds`, which only this swing writes. Nothing in the game
	 * calls these.
	 */
	bool TrySwingAtForTest(AActor* Target) { return TrySwingAt(Target); }
	float LastSwingSecondsForTest() const { return LastSwingSeconds; }

	/**
	 * A press of the left button on this enemy, from the point where `Input_MoveToCursorStarted` has found it under
	 * the cursor, and the button's release, exactly as play runs them, for automation tests. Ruled 2026-10-16 (skill
	 * gadgets stack 2): with the basic attack converted, the click places its gadget and does not swing. A test world
	 * has no cursor to trace, so the press is given its enemy. CALL THE RELEASE ONLY AFTER A PRESS ON AN ENEMY: after
	 * any other press it goes on to the pick-up and the move order, which trace the cursor. Nothing in the game calls
	 * these.
	 */
	void PressOnAnEnemyForTest(AActor* Enemy) { PressOnAnEnemy(Enemy); }
	void ReleaseTheButtonForTest() { Input_MoveToCursorReleased(); }

	/**
	 * Where a skill this player uses is aimed: the point under the cursor. False when the cursor points at nothing in
	 * the world, and then `UCataclysmSkillTemplate::AimPoint` falls back to the character, whose facing aims the use.
	 * Issue #2870.
	 *
	 * AN ENEMY UNDER THE CURSOR IS AIMED AT ITSELF, at its location, before the floor is asked. WHAT IT FIXES: the floor
	 * trace is on the visibility channel, and the engine's stock `Pawn` and `CharacterMesh` profiles both ignore it, so
	 * a cursor resting on a creature's body passes through it and lands on the floor BEHIND it. The camera looks down at
	 * sixty degrees (`ACataclysmPlayerCharacter`'s boom), so a cursor on a creature's body a metre above the floor lands
	 * about 0.58 m (one metre over the tangent of sixty degrees) beyond its feet, up the screen. For a creature a metre
	 * to the character's left or right on screen that turns the aim about thirty degrees off it, outside the 22.5
	 * either side of the Whip's 45 degree swing; the same creature three metres away is off by about eleven, inside.
	 * That is the owner's "it only hits them if they're on the edge" from the aim's side, beside the cone's own.
	 *
	 * THE SAME TEST THE CLICK ATTACK USES, `EnemyUnderCursor` (the pawn channel, a living hostile that is not hidden),
	 * so a press with the cursor on an enemy aims where a click on it would, which ruling P7-b of 2026-10-16 already
	 * gives the click. An ally, a corpse or the player's own body under the cursor is not an
	 * enemy, and the floor is asked as before.
	 *
	 * NOTHING ELSE CHANGES: no auto-target, no nearest enemy, no lock. With no enemy under the cursor the floor point
	 * is the aim, as it always was, and the character's facing is used only when the cursor points at nothing.
	 */
	bool AimPointUnderCursor(FVector& OutPoint) const;

	/**
	 * What the cursor is over, given by hand for automation tests, which have no cursor to trace: an enemy (or null),
	 * and the floor point the visibility trace would answer. While given, `AimPointUnderCursor` reads these instead of
	 * tracing, and runs its own choice between them unchanged. `ForgetCursorForTest` returns it to tracing. Nothing in
	 * the game calls these. Issue #2870.
	 */
	void PointCursorForTest(AActor* Enemy, const FVector& FloorPoint)
	{
		bCursorGivenForTest = true;
		CursorEnemyForTest = Enemy;
		CursorFloorForTest = FloorPoint;
	}
	void ForgetCursorForTest() { bCursorGivenForTest = false; CursorEnemyForTest.Reset(); }

private:
	/** What `PointCursorForTest` gave, read by `AimPointUnderCursor` while `bCursorGivenForTest` is set. */
	bool bCursorGivenForTest = false;
	TWeakObjectPtr<AActor> CursorEnemyForTest;
	FVector CursorFloorForTest = FVector::ZeroVector;

private:
	/**
	 * What a press of the left button on the world does once it knows the enemy under the cursor, or null for the
	 * floor: remembers whether the press began on an enemy, and on one, places the basic attack's trap or gadget when
	 * that slot is converted (traps layer 2, L1; S15) or else takes it as the target and swings. Split out of
	 * `Input_MoveToCursorStarted` on 2026-10-16 so a test can reach it.
	 */
	void PressOnAnEnemy(AActor* Enemy);

private:

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

	/** The floor object the player clicked from out of reach: its panel opens on arrival, as a drop is taken. */
	void UpdatePendingObject();

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
	 * A SOFT PATH RESOLVED WHEN THE SCREEN IS FIRST ASKED FOR, rather than a
	 * hard reference found in the constructor. The asset is generated by
	 * `tools/generate_interface_assets.py`, so a checkout that has not run it
	 * does not have one -- and a hard reference to a missing class logs a
	 * warning on every single start of the editor, about a screen nobody has
	 * asked for. This way the complaint arrives when the screen is asked for,
	 * by a key or a console command, and says what to run.
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

	/** Which Widget Blueprint the empire upgrade tree screen is. Soft, for the reason the character creator's is.
	 *  Issue #2741 slice 2; `tools/generate_interface_assets.py` makes it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmEmpireTreeWidget> EmpireTreeScreenClass =
		TSoftClassPtr<UCataclysmEmpireTreeWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_EmpireTree.WBP_EmpireTree_C")));

	/** The empire upgrade tree screen, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<UCataclysmEmpireTreeWidget> EmpireTreeScreen = nullptr;

	/** Which Widget Blueprint the forge's screen is. Soft, for the reason the character creator's is.
	 *  `tools/generate_interface_assets.py` makes it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmForgeWidget> ForgeScreenClass =
		TSoftClassPtr<UCataclysmForgeWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_Forge.WBP_Forge_C")));

	/** The forge's screen, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<UCataclysmForgeWidget> ForgeScreen = nullptr;

	/** Which Widget Blueprint the merchant's screen is. Soft, for the reason the forge's is. Issue #48, slice S1. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<class UCataclysmMerchantWidget> MerchantScreenClass =
		TSoftClassPtr<class UCataclysmMerchantWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_Merchant.WBP_Merchant_C")));

	/** The merchant's screen, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<class UCataclysmMerchantWidget> MerchantScreen = nullptr;

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

	/** Which Widget Blueprint the skill-choice screen is. Soft, for the reason the character creator's is. Issue #837
	 *  layer 4. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<class UCataclysmSkillAssignmentWidget> SkillAssignmentScreenClass =
		TSoftClassPtr<class UCataclysmSkillAssignmentWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_SkillAssignment.WBP_SkillAssignment_C")));

	/** The skill-choice screen, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<class UCataclysmSkillAssignmentWidget> SkillAssignmentScreen = nullptr;

	/** Which Widget Blueprint the loot filter screen is. Soft, for the reason the character creator's is. Issue #2846,
	 *  slice LF2. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<class UCataclysmLootFilterWidget> LootFilterScreenClass =
		TSoftClassPtr<class UCataclysmLootFilterWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_LootFilter.WBP_LootFilter_C")));

	/** The loot filter screen, once it has been opened at least once. */
	UPROPERTY()
	TObjectPtr<class UCataclysmLootFilterWidget> LootFilterScreen = nullptr;

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

	/** Which Widget Blueprint the choice panel is. Soft, for the reason the character creator's is. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<class UCataclysmChoicePanelWidget> ChoicePanelClass =
		TSoftClassPtr<class UCataclysmChoicePanelWidget>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_ChoicePanel.WBP_ChoicePanel_C")));

	/** The choice panel, once it has been opened at least once. One widget for every floor object, told which. */
	UPROPERTY()
	TObjectPtr<class UCataclysmChoicePanelWidget> ChoicePanel = nullptr;

	/** Which Widget Blueprint the floor's modifier panel is. Soft, for the reason
	 *  the character creator's is. Issue #41. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Interface")
	TSoftClassPtr<UCataclysmFloorModifierPanel> FloorModifierPanelClass =
		TSoftClassPtr<UCataclysmFloorModifierPanel>(FSoftObjectPath(
			TEXT("/Game/Interface/WBP_FloorModifiers.WBP_FloorModifiers_C")));

	/**
	 * The floor's modifier panel, once a floor carrying modifiers has been
	 * walked.
	 *
	 * ONE WIDGET FOR EVERY FLOOR, told what the floor carries each time one
	 * begins, for the reason the city screen is one widget for 25 cities.
	 */
	UPROPERTY()
	TObjectPtr<UCataclysmFloorModifierPanel> FloorModifierPanel = nullptr;

	/** The ground hover's tooltip, made the first time a drop is hovered. See `RefreshGroundItemHover`. Issue #2807. */
	UPROPERTY()
	TObjectPtr<UCataclysmGroundItemTooltip> GroundItemTooltip = nullptr;

	/** The drop the ground hover's tooltip was last worked out for, and the equipment's change count then. */
	TWeakObjectPtr<ACataclysmDroppedItem> GroundTooltipShows;
	int32 GroundTooltipShownAtWornChanges = -1;

	/**
	 * The stand-in the ground hover compares with: ITS OWN, spawned by `RefreshGroundItemHover` the first time a piece
	 * of gear is hovered, whether or not the inventory has ever been opened. Transient and hidden
	 * (`ACataclysmComparisonStandIn::SpawnIn`), so it goes with its world: a level change destroys it, this weak
	 * pointer reads null, and the next hover spawns one in the new world. The inventory screen keeps its own the same way.
	 */
	TWeakObjectPtr<ACataclysmComparisonStandIn> GroundStandIn;

	/** See `FloorModifiersShown`. Issue #1841. */
	FCataclysmFloorModifiersShown LastFloorModifiersShown;

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

	/** The floor object the player is walking to after a click from out of reach. Weak, for `PendingPickup`'s reason. */
	TWeakObjectPtr<class ACataclysmFloorObject> PendingObject;

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

	/** Whether Alt, the key that shows the drops the loot filter hides, is held. Issue #2846, slice LF1. */
	bool bRevealHiddenDrops = false;

	/** Puts the pause screen in the viewport, made the first time; nothing without a local player, as in a test. */
	void ShowPauseScreen();

	/** Takes the pause screen out of the viewport, if it is in it. */
	void HidePauseScreen();

	/** The pause screen, once the game has been paused at least once. A UPROPERTY so it is kept between pauses. */
	UPROPERTY()
	TObjectPtr<UCataclysmPauseScreenWidget> PauseScreen = nullptr;

	/** See `IsPauseScreenWanted`. */
	bool bPauseScreenWanted = false;

	/** See `IsHeldUntilFirstInput`. Issue #2970. */
	bool bHeldUntilFirstInput = false;

	/** See `SetAScreenOpenForTest`. */
	bool bScreenOpenForTest = false;
};
