// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmFloorSourceCharacter.h"

ACataclysmFloorSourceCharacter::ACataclysmFloorSourceCharacter()
{
	// NO BRAIN. Without a controller nothing moves it, turns it or attacks with it; see the class
	// comment.
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
}
