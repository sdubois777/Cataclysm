// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmChorusSourceCharacter.generated.h"

/**
 * The source of an Eternal Chorus: what the player destroys to silence it. Issues #1820 and #41.
 *
 * "Players must destroy the source of the hymn to silence it." THE LEAST CREATURE THAT ANSWERS THAT
 * SENTENCE, AND DELIBERATELY NOT A DESTRUCTIBLE OBJECT. Only a creature can be targeted today -- it
 * needs pawn collision, an ability system, a hostile team and health, and nothing else on a floor has
 * all four -- so the source is one. A real destructible-object mechanism stays unbuilt, and a later row
 * may still need it. Ruled by the coordinating session under the owner's delegation, 2026-09-24.
 *
 * IT DOES NOTHING. No brain (`AutoPossessAI` is disabled and there is no controller class), so it never
 * moves, turns, attacks or uses an ability: `AttackTarget` and `UseEnemyAbility` are only ever called by
 * a brain, and `EnemyAbilities` is the base's empty list. It wears the base enemy's placeholder cylinder,
 * which is engine content and not art.
 *
 * NO ARCHETYPE ROW. A class naming one must name a row of `game/Data/EnemyArchetypes.csv`, which is
 * generated from the design workbook, so this one names none, as the base class does. The save system
 * skips this class when it maps archetype names to classes, so the sandbox's training dummy keeps sole
 * claim to that empty name (`FCataclysmSaveApply`), and a saved floor never holds one anyway: the dungeon
 * game mode marks every source as raised by a rule, and `FCataclysmSaveGather::FloorFrom` skips those.
 */
UCLASS()
class CATACLYSM_API ACataclysmChorusSourceCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmChorusSourceCharacter();
};
