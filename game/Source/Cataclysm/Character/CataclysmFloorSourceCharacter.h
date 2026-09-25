// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "CataclysmFloorSourceCharacter.generated.h"

/**
 * Something a dungeon rule places on a floor for the player to destroy, and which does nothing
 * itself: an Eternal Chorus's source, a Necrotic Bloom's flower. Issues #1820 and #41.
 *
 * A CREATURE, AND DELIBERATELY NOT A DESTRUCTIBLE OBJECT. Only a creature can be targeted today -- it
 * needs pawn collision, an ability system, a hostile team and health, and nothing else on a floor has
 * all four. A real destructible-object mechanism stays unbuilt. Ruled by the coordinating session under
 * the owner's delegation, 2026-09-24, for the Chorus, and made a shared class for the Bloom on
 * 2026-09-25 because more rows want one.
 *
 * IT DOES NOTHING. No brain (`AutoPossessAI` is disabled and there is no controller class), so it never
 * moves, turns, attacks or uses an ability: `AttackTarget` and `UseEnemyAbility` are only ever called by
 * a brain, and `EnemyAbilities` is the base's empty list. It wears the base enemy's placeholder cylinder,
 * which is engine content and not art.
 *
 * NO ARCHETYPE ROW. A class naming one must name a row of `game/Data/EnemyArchetypes.csv`, which is
 * generated from the design workbook, so this one names none, as the base class does. The save system
 * skips this class and every class below it when it maps archetype names to classes, so the sandbox's
 * training dummy keeps sole claim to that empty name (`FCataclysmSaveApply`), and a saved floor never
 * holds one anyway: the dungeon game mode marks each as raised by a rule, and
 * `FCataclysmSaveGather::FloorFrom` skips those.
 *
 * ABSTRACT: a rule places one of the classes below, which the health bar names.
 */
UCLASS(Abstract)
class CATACLYSM_API ACataclysmFloorSourceCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmFloorSourceCharacter();
};
