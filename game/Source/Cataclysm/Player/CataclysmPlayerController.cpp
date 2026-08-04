// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
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

void ACataclysmPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	if (UCataclysmAbilitySystemComponent* ASC = GetCataclysmAbilitySystem())
	{
		if (bGamePaused)
		{
			// Menus and the empire layer's day clock both pause the world. A key
			// held across the pause must not activate on the far side of it.
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
	if (!ControlledPawn || bStandStill)
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

void ACataclysmPlayerController::Input_MoveToCursorStarted()
{
	StopMovement();
	FollowTime = 0.0f;
	UpdateCachedDestination();
}

void ACataclysmPlayerController::Input_MoveToCursorHeld()
{
	FollowTime += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;

	if (!UpdateCachedDestination())
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || bStandStill)
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
	// Released quickly: treat it as a click and path to the point. Released after
	// a hold: the steering above already happened and there is nothing to add.
	if (FollowTime <= ShortPressThreshold && !bStandStill)
	{
		UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, CachedDestination);
	}

	FollowTime = 0.0f;
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
