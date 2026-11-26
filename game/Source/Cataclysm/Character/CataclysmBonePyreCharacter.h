// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmBonePyreCharacter.generated.h"

/**
 * A bone pyre: what a creature carrying Death's Funeral Pyre leaves at the feet of whoever struck it, and which
 * explodes unless it is destroyed first. "Summon deadly bone pyres when hit that explode if not destroyed quickly."
 * Written 2026-11-24, ahead of its window.
 *
 * A FLOOR SOURCE, because only a creature can be targeted (`ACataclysmFloorSourceCharacter` says why), and a pyre must
 * be destroyable. It has no brain and does nothing itself; its fuse runs on the per-character step every character
 * already has, through `UCataclysmEnemyModifiers::PyreStep`.
 *
 * ITS OWN CLASS so the status line can name it and the step can find it. Placed by
 * `UCataclysmEnemyModifiers::PyreWhenStruck`, which marks it as raised by a rule and as dying unpaid: a save does not
 * keep it, and destroying it drops nothing.
 */
UCLASS()
class CATACLYSM_API ACataclysmBonePyreCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()

public:
	/** Seconds until it explodes. Nought until it is placed, and a pyre with nought left explodes on its next step. */
	float PyreFuseSecondsLeft = 0.0f;

	/** What its explosion deals each character it catches, before their defences: fixed when it is placed. */
	float PyreExplosionDamage = 0.0f;

	/** The creature that placed it. Its explosion is its own blow, so the creature may be gone by then. */
	TWeakObjectPtr<AActor> PyreKindledBy;

	/** Whether it has exploded. Set before the blast, so nothing the blast sets off can explode it a second time. */
	bool bPyreSpent = false;
};
