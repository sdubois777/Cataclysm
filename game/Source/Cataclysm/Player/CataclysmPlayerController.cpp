// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Interface/CataclysmHUD.h"
#include "Interface/CataclysmCharacterCreationWidget.h"
#include "Interface/CataclysmEmpireMapWidget.h"
#include "Interface/CataclysmInventoryWidget.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmWearing.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Input/CataclysmInputComponent.h"
#include "Input/CataclysmInputConfig.h"
#include "EngineUtils.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Cataclysm.h"

ACataclysmPlayerController::ACataclysmPlayerController()
{
	// Click-to-move needs somewhere to walk the path it finds. Without this
	// component SimpleMoveToLocation creates one on demand, but creating it here
	// means it exists before the first click rather than during it.
	CreateDefaultSubobject<UPathFollowingComponent>(TEXT("PathFollowingComponent"));

	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ACataclysmPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// SET THE MODE RATHER THAN INHERIT ONE. Issue #1015. Before this, no code in
	// the project stated what input mode the game plays in: it ran in whatever
	// the engine defaults to, and the first screen to close replaced that with
	// something nothing had chosen. Setting it here means the mode a screen
	// restores IS the mode the game started in, because they are one function.
	ApplyPlayingInputMode();
}

void ACataclysmPlayerController::ApplyPlayingInputMode()
{
	// BOTH OPTIONS ARE LOAD-BEARING AND NEITHER IS TIDINESS.
	//
	// DoNotLock, because a cursor locked to the viewport cannot leave it, and
	// this is a windowed game a player alt-tabs out of.
	//
	// HideCursorDuringCapture(false), because holding the move button captures
	// the mouse, and a cursor hidden during that capture is a cursor
	// `UpdateCachedDestination` cannot trace under -- which is click-to-move
	// steering nowhere for as long as the button is held.
	SetInputMode(FInputModeGameAndUI()
					 .SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock)
					 .SetHideCursorDuringCapture(false));
}

UCataclysmAbilitySystemComponent* ACataclysmPlayerController::GetCataclysmAbilitySystem() const
{
	const ACataclysmPlayerState* PS = GetPlayerState<ACataclysmPlayerState>();
	return PS ? PS->GetCataclysmAbilitySystemComponent() : nullptr;
}

void ACataclysmPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Input only exists on the machine the player is sitting at. On a server
	// simulating a remote client there is no local player and no subsystem.
	if (!IsLocalPlayerController())
	{
		return;
	}

	const UCataclysmInputConfig* Config = InputConfig.LoadSynchronous();
	if (!Config)
	{
		// Loudly, because the symptom otherwise is a character that does nothing
		// at all with no indication that the cause is a missing asset path.
		UE_LOG(LogCataclysm, Error,
			TEXT("No input config. Set InputConfig under [/Script/Cataclysm.CataclysmPlayerController] ")
			TEXT("in Config/DefaultGame.ini, and run tools/generate_input_assets.py if the asset is absent."));
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (UInputMappingContext* Context = DefaultMappingContext.LoadSynchronous())
		{
			Subsystem->AddMappingContext(Context, /*Priority=*/0);
		}
		else
		{
			UE_LOG(LogCataclysm, Error,
				TEXT("No input mapping context. No key is bound to anything."));
		}
	}

	UCataclysmInputComponent* Input = Cast<UCataclysmInputComponent>(InputComponent);
	if (!Input)
	{
		// DefaultInputComponentClass in Config/DefaultInput.ini decides this
		// class. If it is left at the engine default, every binding below is
		// skipped and no key does anything.
		UE_LOG(LogCataclysm, Error,
			TEXT("InputComponent is %s, not UCataclysmInputComponent. Check DefaultInputComponentClass ")
			TEXT("in Config/DefaultInput.ini."), *GetNameSafe(InputComponent));
		return;
	}

	// Fully qualified rather than pulled in with a using-directive. Unreal
	// declares a global template named Move in UnrealTemplate.h, so an unqualified
	// Move here is ambiguous and does not compile.
	namespace Names = CataclysmInputActionNames;

	Input->BindNativeAction(Config, Names::Move, ETriggerEvent::Triggered,
		this, &ACataclysmPlayerController::Input_Move);

	Input->BindNativeAction(Config, Names::MoveToCursor, ETriggerEvent::Started,
		this, &ACataclysmPlayerController::Input_MoveToCursorStarted);
	Input->BindNativeAction(Config, Names::MoveToCursor, ETriggerEvent::Triggered,
		this, &ACataclysmPlayerController::Input_MoveToCursorHeld);
	Input->BindNativeAction(Config, Names::MoveToCursor, ETriggerEvent::Completed,
		this, &ACataclysmPlayerController::Input_MoveToCursorReleased);
	// Canceled as well as Completed: a press interrupted by the mapping context
	// changing reports Canceled, and without this the follow timer is never
	// reset and the next click is misread as a long hold.
	Input->BindNativeAction(Config, Names::MoveToCursor, ETriggerEvent::Canceled,
		this, &ACataclysmPlayerController::Input_MoveToCursorReleased);

	Input->BindNativeAction(Config, Names::StandStill, ETriggerEvent::Started,
		this, &ACataclysmPlayerController::Input_StandStillStarted);
	Input->BindNativeAction(Config, Names::StandStill, ETriggerEvent::Completed,
		this, &ACataclysmPlayerController::Input_StandStillReleased);

	Input->BindNativeAction(Config, Names::Zoom, ETriggerEvent::Triggered,
		this, &ACataclysmPlayerController::Input_Zoom);

	// Started rather than Completed, so the screen opens as the key goes down
	// the way a screen does everywhere else, rather than when it comes back up.
	Input->BindNativeAction(Config, Names::ToggleInventory, ETriggerEvent::Started,
		this, &ACataclysmPlayerController::Input_ToggleInventory);

	Input->BindNativeAction(Config, Names::ToggleCharacterCreation,
		ETriggerEvent::Started, this,
		&ACataclysmPlayerController::Input_ToggleCharacterCreation);

	Input->BindNativeAction(Config, Names::TogglePassiveTree,
		ETriggerEvent::Started, this,
		&ACataclysmPlayerController::Input_TogglePassiveTree);

	TArray<uint32> BindHandles;
	Input->BindAbilityActions(Config, this,
		&ACataclysmPlayerController::Input_AbilitySlotPressed,
		&ACataclysmPlayerController::Input_AbilitySlotReleased,
		BindHandles);
}

void ACataclysmPlayerController::OnUnPossess()
{
	// The pawn is going away while keys may still be held. Anything waiting on a
	// release has to be told now, because no release event will arrive.
	if (UCataclysmAbilitySystemComponent* ASC = GetCataclysmAbilitySystem())
	{
		ASC->ClearAbilityInput();
	}

	Super::OnUnPossess();
}

bool ACataclysmPlayerController::IsPawnStunned() const
{
	// KNOCKED DOWN AS WELL AS STUNNED, which is what `CannotAct` answers. Section
	// VI of the design document puts the two in one row of its table of hard
	// stops, so everything this gates -- movement, item pickup and skill input --
	// is refused for both.
	//
	// THE FUNCTION KEEPS ITS NAME, because a stun is still the only thing in the
	// game that reaches a PLAYER: all three rows that knock down are player
	// skills aimed at enemies. Renaming it would rename it in the header, both
	// tests that call it and every log line, to describe a case nothing can
	// currently produce.
	return UCataclysmSkillEffects::CannotAct(GetPawn());
}

bool ACataclysmPlayerController::IsPawnPinned() const
{
	return UCataclysmSkillEffects::IsPinned(GetPawn());
}

bool ACataclysmPlayerController::PawnCannotWalk() const
{
	// WHAT EVERY MOVEMENT GATE ASKS, AND NO ABILITY GATE DOES. A hard stop
	// refuses everything; a pin refuses travel and leaves casting, swinging and
	// picking up alone. Four movement sites and one ability site read these two
	// questions, and getting one of the five wrong is the mistake this pair of
	// functions exists to make visible: a movement site asking `IsPawnStunned`
	// lets a pinned player walk, and an ability site asking this one takes a
	// pinned player's skills away.
	return IsPawnStunned() || IsPawnPinned();
}

void ACataclysmPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	// A STUN HAS TO CANCEL MOVEMENT ALREADY UNDER WAY, NOT ONLY REFUSE NEW
	// MOVEMENT. A click-to-move order hands the pawn to the path following
	// component, which keeps walking the route with no further input, so a
	// player stunned mid-walk would stroll through the whole stun. This is the
	// same reason Input_StandStillStarted calls StopMovement rather than only
	// suppressing the handlers.
	//
	// HERE RATHER THAN IN THE HANDLERS because this runs every frame and the
	// handlers only run when a key is down. A stun that lands while nothing is
	// pressed still has to stop the pawn.
	const bool bStunned = IsPawnStunned();

	// A PIN STOPS THE WALK AND NOTHING ELSE, and that is the whole difference
	// between the two. A pinned character still casts, still swings and still
	// picks up what is already at its feet; it cannot travel. So this joins the
	// movement half below and is deliberately absent from the ability half.
	//
	// NOTHING APPLIES A PIN TO THE PLAYER TODAY. All four rows that pin are
	// player skills aimed at enemies. It is wired here because the tag is applied
	// by a function that takes "a target" rather than "an enemy", the same way
	// `UCataclysmSkillEffects::ApplyKnockback` was written for both directions,
	// and because a pin that silently did nothing to the player would be found
	// the day an enemy ability first states one.
	if (PawnCannotWalk())
	{
		StopMovement();

		// A STUN ABANDONS THE ITEM TOO. The walk toward it has just been
		// cancelled, so continuing to wait for an arrival that will never come
		// would take the item the moment the player next wandered near it.
		PendingPickup = nullptr;
	}

	UpdatePendingPickup();
	CollectMaterialsNearby();

	if (UCataclysmAbilitySystemComponent* ASC = GetCataclysmAbilitySystem())
	{
		if (bGamePaused || bStunned)
		{
			// Menus and the empire layer's day clock both pause the world. A key
			// held across the pause must not activate on the far side of it, and
			// a key held across a stun must not activate on the far side of the
			// stun either -- the design's stun means the target cannot act at
			// all, so a skill fired the instant it ends was not the player
			// reacting.
			ASC->ClearAbilityInput();
		}
		else
		{
			ASC->ProcessAbilityInput();
		}
	}

	Super::PostProcessInput(DeltaTime, bGamePaused);
}

void ACataclysmPlayerController::Input_AbilitySlotPressed(FGameplayTag SlotTag)
{
	// A MOUSE PRESS THAT LANDS ON AN OPEN SCREEN IS A SCREEN CLICK AND NOT A
	// SKILL. The left button has worked that way since issue #731. The right
	// button did not, so right clicking an item both failed to wear it and
	// fired the Heavy ability at whatever floor lay behind the panel.
	// Issue #853.
	//
	// ASKED BY KEY RATHER THAN BY SLOT, so it stays true if a mapping context
	// moves the Heavy ability to another button, and so a keyboard slot is
	// untouched: pressing Q with the bag open still casts.
	//
	// BEFORE THE STUN CHECK, matching the left button, which diverts in
	// Input_MoveToCursorStarted before anything asks whether the character can
	// act. Rearranging a bag is not an action the character takes.
	if (KeyForAbilitySlot(SlotTag).IsMouseButton() && CursorIsOverInterface())
	{
		// ONCE PER PRESS, NOT ONCE PER FRAME, AND THAT IS THE WHOLE OF ISSUE
		// #1016. `BindAbilityActions` binds this function to
		// `ETriggerEvent::Triggered`, which fires EVERY FRAME the button is
		// held. That is right for an ability -- holding the button keeps casting
		// -- and wrong for a click on a screen.
		//
		// WHY IT ONLY SHOWED WHEN THE TARGET SLOT WAS ALREADY FULL, which is
		// what the project owner noticed and what made this findable.
		// `UCataclysmWearing::WearFromCarried` empties the carried cell and then
		// puts whatever came off the body into the first free slot -- which is
		// usually the cell just emptied. So the second frame of the same press
		// found the OLD item sitting in the cell and swapped it back on, the
		// third swapped it off again, and so on. Whether the player ended up
		// wearing what they clicked depended on whether they held the button for
		// an odd or an even number of frames.
		//
		// AN EMPTY TARGET SLOT NEVER SHOWED IT, because nothing comes off, so
		// the cell stays empty and every later frame of the press is refused
		// with "There is nothing there to wear". The play test log on
		// 2026-08-26 is full of those in runs of consecutive frames, which is
		// what a working press looks like under this defect.
		if (SlotsAlreadyPressedOnTheInventory.Contains(SlotTag))
		{
			return;
		}
		SlotsAlreadyPressedOnTheInventory.Add(SlotTag);

		RightPressOnTheInventoryScreen();
		return;
	}

	// A stunned character cannot act at all, and that includes skills. The
	// press is dropped rather than queued, so nothing fires when it wears off.
	if (IsPawnStunned())
	{
		return;
	}

	if (UCataclysmAbilitySystemComponent* ASC = GetCataclysmAbilitySystem())
	{
		ASC->AbilityInputTagPressed(SlotTag);
	}
}

void ACataclysmPlayerController::Input_AbilitySlotReleased(FGameplayTag SlotTag)
{
	// THE PRESS IS OVER, SO THE NEXT ONE MAY ACT ON THE SCREEN AGAIN. Issue
	// #1016. Cleared unconditionally rather than only when the cursor is still
	// over the screen: a player who presses on a cell and drags off it has still
	// finished that press, and leaving the tag here would make the NEXT press do
	// nothing. That is the same reasoning `Input_MoveToCursorReleased` gives for
	// clearing `bPressBeganOnInterface` the way it does.
	//
	// `BindAbilityActions` BINDS THIS TO Canceled AS WELL AS Completed, so a
	// press interrupted by a mapping context change cannot leave a tag stuck
	// here and make the button appear dead. The left button's own binding
	// already did that for the same reason.
	SlotsAlreadyPressedOnTheInventory.Remove(SlotTag);

	if (UCataclysmAbilitySystemComponent* ASC = GetCataclysmAbilitySystem())
	{
		ASC->AbilityInputTagReleased(SlotTag);
	}
}

void ACataclysmPlayerController::Input_Move(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || bStandStill || PawnCannotWalk())
	{
		return;
	}

	// Directional movement cancels any path the character is following, so the
	// two schemes cannot fight each other for the same frame.
	StopMovement();

	const FVector2D Axis = Value.Get<FVector2D>();

	// Movement is relative to the camera, not to the character. The camera does
	// not rotate with the character -- the spring arm uses absolute rotation --
	// so world axes and camera axes are the same thing, and pressing W moves up
	// the screen from wherever the character happens to be facing.
	ControlledPawn->AddMovementInput(FVector::ForwardVector, Axis.Y);
	ControlledPawn->AddMovementInput(FVector::RightVector, Axis.X);
}

void ACataclysmPlayerController::Input_ToggleInventory()
{
	if (!InventoryScreen)
	{
		InventoryScreen =
			CreateWidget<UCataclysmInventoryWidget>(this,
				UCataclysmInventoryWidget::StaticClass());
		if (!InventoryScreen)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("The carried inventory screen could not be created, so "
						"the key that opens it does nothing."));
			return;
		}
	}

	if (InventoryScreen->IsInViewport())
	{
		// NOTHING STAYS ON THE CURSOR ONCE THE SCREEN IS GONE. A held item
		// never left its slot, so this loses nothing; it stops the next
		// opening from beginning part way through a move. Issue #853.
		if (ACataclysmPlayerCharacter* Wearer =
				Cast<ACataclysmPlayerCharacter>(GetPawn()))
		{
			UCataclysmWearing::ReleaseHeld(Wearer->GetInventory());
		}
		InventoryScreen->RemoveFromParent();
		return;
	}

	InventoryScreen->AddToViewport();
}

void ACataclysmPlayerController::Input_ToggleCharacterCreation()
{
	ToggleCharacterCreation();
}

void ACataclysmPlayerController::ToggleCharacterCreation()
{
	if (!CharacterCreationScreen)
	{
		// LOADED SYNCHRONOUSLY, WHICH IS SAFE HERE AND WOULD NOT BE IN A DRAW
		// PASS. `KeyForAbilitySlot` below deliberately uses `Get` rather than
		// `LoadSynchronous` because it runs while the heads-up display is
		// drawing. This runs once, from a key press, at a moment when the
		// player has asked to see a screen and is waiting for one.
		UClass* ScreenClass = CharacterCreationScreenClass.LoadSynchronous();
		if (!ScreenClass)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("There is no character creator to open: %s could not be "
						"loaded. Run  python tools/run_editor_python.py "
						"tools/generate_interface_assets.py  to build it."),
				   *CharacterCreationScreenClass.ToString());
			return;
		}

		CharacterCreationScreen =
			CreateWidget<UCataclysmCharacterCreationWidget>(this, ScreenClass);
		if (!CharacterCreationScreen)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("The character creator could not be created, so the key "
						"that opens it does nothing."));
			return;
		}
	}

	if (CharacterCreationScreen->IsInViewport())
	{
		// CLOSING IS NOT CONFIRMING. Whatever was half-chosen stays on the
		// widget and is there again when the screen is reopened, and nothing
		// about the character has changed. Only the confirm button changes
		// anything, which is why it is the only other thing that closes the
		// screen.
		CharacterCreationScreen->RemoveFromParent();

		// BACK TO THE MODE THE GAME PLAYS IN, which since issue #1015 is the
		// same mode it was already in. This used to set `FInputModeGameOnly`,
		// and that is what stopped the player moving after closing a screen.
		ApplyPlayingInputMode();
		return;
	}

	// THE FIRST SCREEN IN THIS PROJECT DRIVEN BY CLICKING ITS OWN BUTTONS, and
	// that is why the mode matters here at all: the buttons are clicked rather
	// than hit-tested by this controller, as the inventory screen's are.
	//
	// IT IS THE SAME CALL AS CLOSING SINCE ISSUE #1015, and that is the point.
	// The game plays in GameAndUI, so a screen that needs GameAndUI needs no
	// change at all -- it is written here so that opening and closing name one
	// mode rather than two, and a reader is not left wondering which is which.
	ApplyPlayingInputMode();

	CharacterCreationScreen->AddToViewport();
}

void ACataclysmPlayerController::ZoomPassiveTree(float Notches)
{
	if (!PassiveTreeScreen)
	{
		return;
	}

	if (FMath::IsNearlyZero(Notches))
	{
		PassiveTreeScreen->FitToTree();
		return;
	}

	PassiveTreeScreen->ZoomBy(Notches);
}

void ACataclysmPlayerController::Input_TogglePassiveTree()
{
	TogglePassiveTree();
}

void ACataclysmPlayerController::TogglePassiveTree()
{
	if (!PassiveTreeScreen)
	{
		UClass* ScreenClass = PassiveTreeScreenClass.LoadSynchronous();
		if (!ScreenClass)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("There is no passive tree screen to open: %s could not "
						"be loaded. Run  python tools/run_editor_python.py "
						"tools/generate_interface_assets.py  to build it."),
				   *PassiveTreeScreenClass.ToString());
			return;
		}

		PassiveTreeScreen =
			CreateWidget<UCataclysmPassiveTreeWidget>(this, ScreenClass);
		if (!PassiveTreeScreen)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("The passive tree screen could not be created, so the "
						"key that opens it does nothing."));
			return;
		}
	}

	if (PassiveTreeScreen->IsInViewport())
	{
		PassiveTreeScreen->RemoveFromParent();

		// THIS LINE IS THE ONE THE PROJECT OWNER FELT. Issue #1015. It used to
		// set `FInputModeGameOnly`, so closing the tree after spending a point
		// left the player unable to move, and reopening the tree brought
		// movement back because opening set GameAndUI again.
		ApplyPlayingInputMode();
		return;
	}

	// THE SAME CALL AS CLOSING, which is what issue #1015 was about. A click has
	// to reach a node rather than the floor behind it, and the game already
	// plays in the mode that allows that.
	ApplyPlayingInputMode();

	// REFRESHED ON EVERY OPENING, not only when it is made. The character's
	// level, its damage type and what it has spent can all have moved since the
	// screen was last closed, and the widget is kept rather than rebuilt.
	PassiveTreeScreen->RefreshDisplay();
	PassiveTreeScreen->AddToViewport();
}

void ACataclysmPlayerController::ToggleEmpireMap()
{
	if (!EmpireMapScreen)
	{
		UClass* ScreenClass = EmpireMapScreenClass.LoadSynchronous();
		if (!ScreenClass)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("There is no empire overview to open: %s could not be "
						"loaded. Run  python tools/run_editor_python.py "
						"tools/generate_interface_assets.py  to build it."),
				   *EmpireMapScreenClass.ToString());
			return;
		}

		EmpireMapScreen =
			CreateWidget<UCataclysmEmpireMapWidget>(this, ScreenClass);
		if (!EmpireMapScreen)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("The empire overview could not be created, so the "
						"command that opens it does nothing."));
			return;
		}
	}

	if (EmpireMapScreen->IsInViewport())
	{
		EmpireMapScreen->RemoveFromParent();

		// THE SAME INPUT MODE AS CLOSING THE PASSIVE TREE, and for the reason
		// issue #1015 recorded: setting `FInputModeGameOnly` here would leave
		// the player unable to move after closing the screen.
		ApplyPlayingInputMode();
		return;
	}

	ApplyPlayingInputMode();

	// REFRESHED ON EVERY OPENING. Days can have passed since it was last
	// closed, and the widget is kept rather than rebuilt.
	EmpireMapScreen->Refresh();
	EmpireMapScreen->AddToViewport();
}

FKey ACataclysmPlayerController::KeyForAbilitySlot(FGameplayTag SlotTag) const
{
	if (!SlotTag.IsValid())
	{
		return FKey();
	}

	// `Get` RATHER THAN `LoadSynchronous`. See the header: this runs inside the
	// heads-up display's draw pass and a synchronous load there stalls a frame.
	const UCataclysmInputConfig* Config = InputConfig.Get();
	if (!Config)
	{
		return FKey();
	}

	const UInputAction* Action = nullptr;
	for (const FCataclysmAbilityInputAction& Binding : Config->GetAbilityActions())
	{
		if (Binding.SlotTag == SlotTag)
		{
			Action = Binding.InputAction;
			break;
		}
	}

	if (!Action)
	{
		return FKey();
	}

	// NAMED `Local` AND NOT `Player`, because `APlayerController` already has a
	// member called `Player` and shadowing it is a warning this project builds
	// as an error.
	const ULocalPlayer* Local = GetLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Input =
		Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;

	if (!Input)
	{
		// NO SUBSYSTEM MEANS THIS IS NOT A LOCAL PLAYER. A remote client's
		// controller on the server has no keys of its own to report.
		return FKey();
	}

	// ASKED OF THE SUBSYSTEM RATHER THAN OF `DefaultMappingContext`, because the
	// mapping context in force is what actually decides the key, and the two
	// schemes bind the Support slot differently.
	const TArray<FKey> Keys = Input->QueryKeysMappedToAction(Action);
	return Keys.Num() > 0 ? Keys[0] : FKey();
}

bool ACataclysmPlayerController::CursorIsOverInterface() const
{
	if (!InventoryScreen || !InventoryScreen->IsInViewport())
	{
		return false;
	}

	float X = 0.0f;
	float Y = 0.0f;
	if (!GetMousePosition(X, Y))
	{
		// NO CURSOR. A gamepad has none, and no screen can be clicked with one
		// yet; that is part of issue #137.
		return false;
	}

	return InventoryScreen->CursorIsOverPanel(FVector2D(X, Y));
}

bool ACataclysmPlayerController::InventoryPressTarget(
	FVector2D& OutPoint, ACataclysmPlayerCharacter*& OutWearer) const
{
	if (!InventoryScreen)
	{
		return false;
	}

	float X = 0.0f;
	float Y = 0.0f;
	if (!GetMousePosition(X, Y))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Wearer =
		Cast<ACataclysmPlayerCharacter>(GetPawn());
	if (!Wearer)
	{
		return false;
	}

	OutPoint = FVector2D(X, Y);
	OutWearer = Wearer;
	return true;
}

void ACataclysmPlayerController::ReportInventoryPress(
	ECataclysmWearResult Result) const
{
	switch (Result)
	{
	case ECataclysmWearResult::Worn:
	case ECataclysmWearResult::Swapped:
	case ECataclysmWearResult::TakenOff:
	case ECataclysmWearResult::PickedUp:
	case ECataclysmWearResult::PutDown:
	case ECataclysmWearResult::Exchanged:
		return;
	default:
		UE_LOG(LogCataclysm, Log, TEXT("%s"),
			   *UCataclysmWearing::Explain(Result));
	}
}

void ACataclysmPlayerController::PressOnTheInventoryScreen()
{
	FVector2D Point = FVector2D::ZeroVector;
	ACataclysmPlayerCharacter* Wearer = nullptr;
	if (!InventoryPressTarget(Point, Wearer))
	{
		ReportPressThatFoundNothing(TEXT("left"), Point, /*bHadTarget=*/false);
		return;
	}

	UCataclysmInventoryComponent* Bag = Wearer->GetInventory();

	// THE CARRIED GRID FIRST. The two panels cannot overlap, so the order only
	// decides which is asked first and not which answers.
	const int32 Carried = InventoryScreen->CarriedSlotUnderCursor(Point);
	if (Carried != INDEX_NONE)
	{
		// ONE BUTTON, TWO HALVES OF ONE GESTURE. Holding nothing, a press picks
		// up; holding something, the same press puts it down, exchanging with
		// whatever is in the cell. Pressing the cell it came from puts it back.
		ReportInventoryPress(Bag && Bag->IsHolding()
			? UCataclysmWearing::PutDownCarried(Bag, Carried)
			: UCataclysmWearing::PickUpCarried(Bag, Carried));
		return;
	}

	const ECataclysmGearSlot Worn = InventoryScreen->GearSlotUnderCursor(Point);
	if (Worn != ECataclysmGearSlot::Count)
	{
		// A WORN PIECE COMES OFF ONTO THE CURSOR. Putting one back ON with a
		// left press is not defined by docs/Inventory_Screen_Design.md, whose
		// table gives left only "pick it up off the body the same way", so a
		// press on a gear slot while holding something is refused rather than
		// given behaviour nobody chose.
		ReportInventoryPress(UCataclysmWearing::PickUpWorn(
			Bag, Wearer->GetEquipment(), Worn));
		return;
	}

	// SAME REPORT AS THE RIGHT BUTTON. Issue #1016 is about equipping, which is
	// the right button, but the two share every step up to this point and a
	// left press that lands on nothing is the same silence.
	ReportPressThatFoundNothing(TEXT("left"), Point, /*bHadTarget=*/true);
}

void ACataclysmPlayerController::RightPressOnTheInventoryScreen()
{
	FVector2D Point = FVector2D::ZeroVector;
	ACataclysmPlayerCharacter* Wearer = nullptr;
	if (!InventoryPressTarget(Point, Wearer))
	{
		ReportPressThatFoundNothing(TEXT("right"), Point, /*bHadTarget=*/false);
		return;
	}

	const int32 Carried = InventoryScreen->CarriedSlotUnderCursor(Point);
	if (Carried != INDEX_NONE)
	{
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		ReportInventoryPress(UCataclysmWearing::WearFromCarried(
			Wearer->GetInventory(), Wearer->GetEquipment(), Carried, Went));
		return;
	}

	const ECataclysmGearSlot Worn = InventoryScreen->GearSlotUnderCursor(Point);
	if (Worn != ECataclysmGearSlot::Count)
	{
		ReportInventoryPress(UCataclysmWearing::TakeOffInto(
			Wearer->GetInventory(), Wearer->GetEquipment(), Worn));
		return;
	}

	// THE PRESS REACHED THE SCREEN AND LANDED ON NO CELL. Issue #1016: the
	// project owner reports that equipping often does nothing on the first
	// right click while the item's tooltip shows correctly, which means Slate's
	// own hit test finds the cell and this one does not.
	ReportPressThatFoundNothing(TEXT("right"), Point, /*bHadTarget=*/true);
}

void ACataclysmPlayerController::ReportPressThatFoundNothing(
	const TCHAR* Button, const FVector2D& Point, bool bHadTarget) const
{
	// WHY THIS EXISTS AT ALL. Issue #1016. There were two ways to press a cell
	// and have nothing happen, and BOTH WERE SILENT: a press that could not read
	// a cursor position, and a press whose position landed in no cell. From a
	// player's side the two are identical and identical to a press that was
	// never delivered, so a report of "it takes several tries" could not be
	// turned into a cause without guessing.
	//
	// LOG RATHER THAN VERBOSE, DELIBERATELY. This only fires when a press did
	// nothing, which is not supposed to happen, so it cannot spam a working
	// game -- and a verbose line is one the project owner would have to know to
	// turn on before reproducing the fault.
	//
	// IT PRINTS THE POINT AND WHAT EACH TEST SAID, because those are the two
	// things that distinguish the remaining causes: a cursor position that could
	// not be read at all, one that the panel test accepted and the cell test
	// did not, and one that both refused.
	if (!bHadTarget)
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("An inventory %s press did nothing: there was no cursor "
					"position, no open screen, or no pawn to act on."),
			   Button);
		return;
	}

	const bool bOverPanel = InventoryScreen
		&& InventoryScreen->CursorIsOverPanel(Point);

	UE_LOG(LogCataclysm, Log,
		   TEXT("An inventory %s press at (%.1f, %.1f) did nothing: it is over "
				"the panel (%s) but over no carried cell and no gear slot. "
				"Issue #1016."),
		   Button, Point.X, Point.Y, bOverPanel ? TEXT("yes") : TEXT("no"));
}

void ACataclysmPlayerController::Input_MoveToCursorStarted()
{
	// A PRESS THAT LANDS ON AN OPEN SCREEN IS NOT A MOVE ORDER, and it does not
	// stop a walk already under way either. The cursor ray goes straight through
	// anything drawn on the canvas, so without this a click on a grid cell would
	// send the character to whatever piece of floor lies behind the panel.
	// Issue #731.
	bPressBeganOnInterface = CursorIsOverInterface();
	if (bPressBeganOnInterface)
	{
		// IT IS NOT A MOVE ORDER, AND SINCE ISSUE #831 IT IS NOT NOTHING
		// EITHER. Since issue #853 a left press picks an item up onto the
		// cursor and puts it down again; wearing moved to the right button.
		PressOnTheInventoryScreen();
		return;
	}

	StopMovement();
	FollowTime = 0.0f;
	UpdateCachedDestination();
}

void ACataclysmPlayerController::Input_MoveToCursorHeld()
{
	// DECIDED AT THE PRESS AND NOT HERE. A player who presses on a cell and
	// drags the cursor off the panel has still not ordered a move, and asking
	// again each frame would start steering the moment the cursor left the edge.
	if (bPressBeganOnInterface)
	{
		return;
	}

	FollowTime += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;

	if (!UpdateCachedDestination())
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || bStandStill || PawnCannotWalk())
	{
		return;
	}

	// Held: steer toward the cursor every frame, without pathfinding. This is
	// what makes holding the button feel like driving the character rather than
	// issuing a series of orders.
	const FVector Direction = (CachedDestination - ControlledPawn->GetActorLocation()).GetSafeNormal();
	ControlledPawn->AddMovementInput(Direction, 1.0f, false);
}

void ACataclysmPlayerController::Input_MoveToCursorReleased()
{
	// THE PRESS BEGAN ON AN OPEN SCREEN, so releasing it finishes nothing in the
	// world. Cleared here and on the Canceled event this is also bound to, so a
	// mapping context change mid-press cannot leave every later click ignored.
	if (bPressBeganOnInterface)
	{
		bPressBeganOnInterface = false;
		FollowTime = 0.0f;
		return;
	}

	// Released quickly: treat it as a click and path to the point. Released after
	// a hold: the steering above already happened and there is nothing to add.
	if (FollowTime <= ShortPressThreshold && !bStandStill && !PawnCannotWalk())
	{
		// A CLICK ON A DROP'S NAME IS A PICK-UP AND NOT A MOVE ORDER. That is
		// the project owner's decision of 2026-08-18: "a player sees a drop as a
		// nametag and clicking on it loots the item". This button otherwise only
		// moves, which is its own decision recorded on this class, and the two do
		// not conflict because a name is a small target that the player has to be
		// pointing at deliberately.
		//
		// ONLY ON A SHORT PRESS. Holding the button steers the character, and
		// dragging the cursor across a name on the way past is not a click on it.
		if (ACataclysmDroppedItem* Clicked = DropUnderCursor())
		{
			APawn* ControlledPawn = GetPawn();
			const bool bInReach = ControlledPawn
				&& UCataclysmDropPickup::IsWithinPickupRange(
					ControlledPawn->GetActorLocation(),
					Clicked->GetActorLocation());

			if (bInReach)
			{
				// CLOSE ENOUGH ALREADY. Whether or not it fits, this click is
				// finished; a full inventory leaves the item where it is.
				TakeDrop(Clicked);
				PendingPickup = nullptr;
			}
			else
			{
				// TOO FAR. Walk there and take it on arrival.
				PendingPickup = Clicked;
				UAIBlueprintHelperLibrary::SimpleMoveToLocation(
					this, Clicked->GetActorLocation());
			}

			FollowTime = 0.0f;
			return;
		}

		// A CLICK ANYWHERE ELSE ABANDONS THE ITEM the player was walking to.
		// They changed their mind, and the walk that follows is a move order
		// rather than the tail of the last one.
		PendingPickup = nullptr;

		UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, CachedDestination);
	}

	FollowTime = 0.0f;
}

ACataclysmDroppedItem* ACataclysmPlayerController::DropUnderCursor() const
{
	const ACataclysmHUD* Overlay = Cast<ACataclysmHUD>(GetHUD());
	if (!Overlay)
	{
		return nullptr;
	}

	float X = 0.0f;
	float Y = 0.0f;
	if (!GetMousePosition(X, Y))
	{
		// NO CURSOR. A gamepad has none, and binding pick-up to a pad is part of
		// issue #137. Answering nullptr makes the click an ordinary move order.
		return nullptr;
	}

	return Overlay->DropUnderPoint(FVector2D(X, Y));
}

bool ACataclysmPlayerController::TakeDrop(ACataclysmDroppedItem* Drop)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !IsValid(Drop))
	{
		return false;
	}

	UCataclysmInventoryComponent* Inventory =
		ControlledPawn->FindComponentByClass<UCataclysmInventoryComponent>();
	if (!Inventory)
	{
		// A PAWN WITH NO INVENTORY CANNOT CARRY ANYTHING. Only the player
		// character has one, and only the player clicks.
		return false;
	}

	if (UCataclysmDropPickup::TakeInto(Inventory, Drop))
	{
		return true;
	}

	// FULL. Said out loud rather than silently ignored, because from the
	// player's side a click that does nothing is indistinguishable from a click
	// that missed. The real answer is a message on screen, which needs the
	// designed interface and is issue #49.
	UE_LOG(LogCataclysm, Warning,
		   TEXT("Inventory full at %d slots, so '%s' stays on the floor."),
		   UCataclysmInventoryComponent::SlotCount, *Drop->DisplayName);
	return false;
}

void ACataclysmPlayerController::UpdatePendingPickup()
{
	ACataclysmDroppedItem* Drop = PendingPickup.Get();
	if (!Drop)
	{
		// EITHER NOTHING WAS CLICKED OR IT IS GONE. Both mean there is nothing
		// left to walk to, and a weak pointer answers the second for free.
		PendingPickup = nullptr;
		return;
	}

	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	if (!UCataclysmDropPickup::IsWithinPickupRange(
			ControlledPawn->GetActorLocation(), Drop->GetActorLocation()))
	{
		// STILL WALKING.
		return;
	}

	// ARRIVED. Taken if it fits; either way this click is finished, so a full
	// inventory does not leave the character trying again every frame.
	TakeDrop(Drop);
	PendingPickup = nullptr;
}

void ACataclysmPlayerController::CollectMaterialsNearby()
{
	const APawn* ControlledPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!ControlledPawn || !World)
	{
		return;
	}

	ACataclysmPlayerCharacter* Wearer =
		Cast<ACataclysmPlayerCharacter>(GetPawn());
	UCataclysmInventoryComponent* Inventory =
		Wearer ? Wearer->GetInventory() : nullptr;
	if (!Inventory)
	{
		return;
	}

	const FVector Standing = ControlledPawn->GetActorLocation();

	// GATHERED BEFORE ANY IS TAKEN. UCataclysmDropPickup::TakeInto destroys
	// the actor it took, and destroying an actor while a TActorIterator is
	// walking the level is not something to rely on.
	//
	// ITERATING EVERY DROP EACH FRAME IS WHAT THE HEADS-UP DISPLAY ALREADY
	// DOES to draw their names, so this adds a second pass over the same small
	// set rather than a new kind of cost.
	TArray<ACataclysmDroppedItem*> Coming;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		ACataclysmDroppedItem* Drop = *It;
		if (IsValid(Drop)
			&& UCataclysmDropPickup::ComesAutomatically(
				   Drop->IsMaterial(), Standing, Drop->GetActorLocation()))
		{
			Coming.Add(Drop);
		}
	}

	for (ACataclysmDroppedItem* Drop : Coming)
	{
		UCataclysmDropPickup::TakeInto(Inventory, Drop);
	}
}

void ACataclysmPlayerController::Input_Zoom(const FInputActionValue& Value)
{
	// Only the player character has a camera boom. A pawn possessed for some
	// other reason, such as a cutscene camera, simply has no zoom.
	// Not named Character: AController already has a member by that name, and
	// shadowing it is an error under this project's warning settings.
	if (ACataclysmPlayerCharacter* PlayerPawn = Cast<ACataclysmPlayerCharacter>(GetPawn()))
	{
		PlayerPawn->AddCameraZoom(Value.Get<float>());
	}
}

void ACataclysmPlayerController::Input_StandStillStarted()
{
	bStandStill = true;

	// Cancel a path already being followed. Without this, holding the modifier
	// stops new movement but the character keeps walking the last route it was
	// given, which reads as the key not working.
	StopMovement();
}

void ACataclysmPlayerController::Input_StandStillReleased()
{
	bStandStill = false;
}

bool ACataclysmPlayerController::UpdateCachedDestination()
{
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility, /*bTraceComplex=*/true, Hit))
	{
		CachedDestination = Hit.Location;
		return true;
	}

	// The cursor is off the world -- over the sky, or past the floor's edge.
	// Keeping the previous destination is better than moving to the origin.
	return false;
}
