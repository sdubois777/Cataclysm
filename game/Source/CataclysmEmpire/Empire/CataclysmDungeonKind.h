// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmDungeonKind.generated.h"

/**
 * The four kinds of dungeon, from `docs/Cataclysm_GDD_v2.md` section VIII.
 *
 * THE ORDER IS THE ORDER THEY GET HARDER IN, and the numbers are deliberate: a
 * saved dungeon or a data table row that stores one of these stores the number,
 * so inserting a kind in the middle later would change what an existing value
 * means. Add to the end.
 *
 * WHERE THIS LIVES AND WHY IT MOVED. It was declared in
 * `game/Source/Cataclysm/Dungeon/CataclysmEnemyScore.h`, which is in the
 * `Cataclysm` module. What kind a dungeon is, is a strategy-layer fact -- the
 * surge scheduler decides it when it puts a dungeon on a city -- and
 * `CataclysmEmpire` must not depend on `Cataclysm`, so the empire layer could
 * not see it. Declaring a second copy in the empire layer would have been two
 * copies of one design fact, which is the exact arrangement this repository has
 * been bitten by twice. Moving it down is what lets both modules name the same
 * kind. Issue #1083.
 */
UENUM(BlueprintType)
enum class ECataclysmDungeonType : uint8
{
	Basic       = 0,
	Quest       = 1,
	FallenCity  = 2	UMETA(DisplayName = "Fallen City"),
	Cataclysm   = 3,
};

/**
 * What a dungeon does differently, from `docs/Cataclysm_GDD_v2.md` section VIII.
 *
 * `None` IS A REAL VALUE AND THE COMMON ONE. Most dungeons have no sub-type at
 * all, and the score model gives it a weight of zero rather than treating it as
 * missing data.
 *
 * TIMED CARRIES NO WEIGHT EITHER, and that is a design statement rather than an
 * oversight: the design document says "A time limit is a constraint on the
 * player rather than on the enemies, so it changes nothing about what an
 * encounter is worth."
 *
 * IT MOVED WITH `ECataclysmDungeonType` AND FOR THE SAME REASON, and the two
 * are kept together because a dungeon's kind and its sub-type are one fact split
 * in two. The empire layer rolls one for every dungeon a surge lands:
 * `UCataclysmSurgeScheduler::RollSubType` is the draw and the `SpawnWeight*`
 * constants beside it are the weights, ported from
 * `config.SUBTYPE_SPAWN_WEIGHTS` in the simulation. Issue #1289.
 *
 * THIS COMMENT SAID NOTHING ROLLED ONE UNTIL 2026-09-06 and pointed at issue
 * #41 as the work that would. That stopped being true when #1289 merged and
 * nothing noticed, because no test reads a comment. Issue #1324's reconnaissance
 * found it.
 */
UENUM(BlueprintType)
enum class ECataclysmDungeonSubType : uint8
{
	None         = 0,
	Timed        = 1,
	Horde        = 2,
	Siege        = 3,
	CowLevel     = 4	UMETA(DisplayName = "Cow Level"),
	Elite        = 5,
	Volatile     = 6,
	Sacrificial  = 7,
};
