// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Interface/CataclysmHUD.h"
#include "Interface/CataclysmInventoryWidget.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Input/CataclysmInputComponent.h"
#include "Input/CataclysmInputConfig.h"
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
	return UCataclysmSkillEffects::IsStunned(GetPawn());
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
	if (bStunned)
	{
		StopMovement();

		// A STUN ABANDONS THE ITEM TOO. The walk toward it has just been
		// cancelled, so continuing to wait for an arrival that will never come
		// would take the item the moment the player next wandered near it.
		PendingPickup = nullptr;
	}

	UpdatePendingPickup();

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
	if (UCataclysmAbilitySystemComponent* ASC = GetCataclysmAbilitySystem())
	{
		ASC->AbilityInputTagReleased(SlotTag);
	}
}

void ACataclysmPlayerController::Input_Move(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || bStandStill || IsPawnStunned())
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
		InventoryScreen->RemoveFromParent();
		return;
	}

	InventoryScreen->AddToViewport();
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
	if (!ControlledPawn || bStandStill || IsPawnStunned())
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
	if (FollowTime <= ShortPressThreshold && !bStandStill && !IsPawnStunned())
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
