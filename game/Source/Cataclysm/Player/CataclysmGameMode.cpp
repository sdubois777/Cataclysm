// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Character/CataclysmPlayerCharacter.h"

ACataclysmGameMode::ACataclysmGameMode()
{
	DefaultPawnClass = ACataclysmPlayerCharacter::StaticClass();
	PlayerControllerClass = ACataclysmPlayerController::StaticClass();

	// The player state is where the ability system component lives, so naming it
	// here is not optional dressing: with the default APlayerState the pawn finds
	// no ability system, InitAbilityActorInfo returns early, and the character
	// spawns with no attributes and no abilities and no error.
	PlayerStateClass = ACataclysmPlayerState::StaticClass();
}
