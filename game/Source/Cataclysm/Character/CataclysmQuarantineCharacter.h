// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmQuarantineCharacter.generated.h"

/**
 * A Quarantine Breach containment: what the player may destroy to release the group it holds. Issues #1820 and
 * #41.
 *
 * "Groups of monsters containing highly infectious diseases have been frozen in time..." A floor source that does
 * nothing itself, with no brain and no archetype row; `ACataclysmFloorSourceCharacter` says why each. Its release
 * and the patches its creatures leave are the dungeon game mode's. Its own class so the health bar can say what it
 * holds.
 */
UCLASS()
class CATACLYSM_API ACataclysmQuarantineCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()

public:
	/** How many it holds and of which kind, as the bar shows them: "5 Imp". Written when it is placed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FString Holds;
};
