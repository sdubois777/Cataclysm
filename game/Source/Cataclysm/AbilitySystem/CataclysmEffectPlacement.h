// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;
class UWorld;

/**
 * Putting an effect that is drawn on the ground onto the ground. Issue #2863.
 *
 * WHY IT EXISTS. The project owner, playtesting on 2026-11-23: "There seems to be
 * skill fx going off under the floor, I only noticed because I got close to an
 * edge and saw something." Nothing that drew on the ground asked where the ground
 * was. Each caller handed over a point and the drawing went exactly there, and the
 * points came from four different places: a character's capsule centre (45 to 155
 * cm above the floor, so a ring floated at waist height), a creature's capsule
 * bottom moved under its target's X and Y (`ACataclysmEnemyController::FloorUnder`,
 * so under the exit stairs, or anywhere the target stood higher than the creature,
 * the patch was drawn inside the ground), a dungeon cell's walking surface, and a
 * stored copy of one of those. `docs/DECISIONS.md`, 2026-11-24, lists every site.
 *
 * ONE PLACE RATHER THAN A FIX AT EACH CALLER. Sixty-eight calls leave a ground zone
 * and five draw a telegraph marker. They all end in three functions --
 * `UCataclysmGroundEffect::PlayFor`, `ACataclysmTelegraphMarker`'s spawn and
 * `ACataclysmDebrisBurst::Scatter` -- and those three ask this, so a caller added
 * later is placed correctly without knowing this exists.
 *
 * ONLY THE DRAWING MOVES. A zone's actor, its sweep, the point an attack is aimed
 * at and where a lobbed shot lands are all where they were; what moves is the
 * height the patch is DRAWN at. A judgement of the writing session: the issue is a
 * drawing in the wrong place, and moving the gameplay points would change what
 * hits what. On a flat floor the two now differ by at most `LiftCm` from a capsule
 * bottom, which is where the marker and the lob landing already were.
 *
 * TWO GAMEPLAY POINTS ASK `FloorSurfaceUnder` AS WELL, since issue #2878, which
 * decided them separately: `ACataclysmTerrain::Spawn` stands every kind of terrain,
 * the Upthrust wall's collision included, on the surface under its near end, and
 * `ACataclysmEnemyController::FloorUnder` answers the surface under its point, so a
 * creature's aim point is no longer inside the exit stairs.
 *
 * A NAMESPACE, NOT A CLASS, like `CataclysmMeshWidth`: there is no state and
 * nothing for the reflection system to see.
 */
namespace CataclysmEffectPlacement
{
	/**
	 * How far above the point asked for the search for the floor begins, in cm.
	 *
	 * ABOVE THE POINT, SO A POINT INSIDE OR UNDER THE GROUND IS LIFTED OUT OF IT.
	 * A trace begun at the point itself would start under the surface it is
	 * looking for and find nothing, or the floor below that. The tallest raised
	 * walking surface on a dungeon floor is the exit stairs' top tier, 60 cm, so a
	 * metre covers it with room to spare.
	 *
	 * NO HIGHER, so a ground patch beside a wall is not drawn on top of it. A
	 * dungeon wall is 400 cm tall; a point at a capsule centre, at most 155.53 cm
	 * up for the Gatekeeper, starts its search at most 255.53 cm up, inside a
	 * wall's height rather than above it. A search that the engine reports as
	 * starting inside a solid shape is refused below; one it does not report
	 * finds no ground under a dungeon wall, which stands beside the floor's
	 * ground blocks rather than on them. Either way the point is left where it
	 * was asked.
	 */
	inline constexpr float LookAboveCm = 100.0f;

	/**
	 * How far below the point asked for the search for the floor ends, in cm.
	 *
	 * DEEP ENOUGH FOR THE TALLEST CAPSULE CENTRE TO REACH THE FLOOR UNDER IT, the
	 * Gatekeeper's 155.53 cm, with room for a body that was in the air when it
	 * left the patch. No deeper, so a point far out over the void past a floor's
	 * edge is not moved onto whatever lies far below it.
	 */
	inline constexpr float LookBelowCm = 300.0f;

	/**
	 * How far above the floor's surface a ground drawing is put, in cm.
	 *
	 * JUST ABOVE, NOT ON. A flat drawing exactly at the surface's height fights the
	 * floor for the same pixels, and which wins changes as the camera moves. Two
	 * centimetres is the gap the engine's character movement keeps between a
	 * capsule's bottom and the floor (1.9 to 2.4 cm), which is where every
	 * telegraph marker in the game was drawn before this existed and nobody saw it
	 * float.
	 */
	inline constexpr float LiftCm = 2.0f;

	/**
	 * The height of the walking surface under `Point`, searched for from
	 * `LookAboveCm` above it to `LookBelowCm` below it.
	 *
	 * A WORLD-STATIC SURFACE, asked by object type as `ACataclysmEnemyCharacter`'s
	 * charge asks for the floor it follows: the dungeon's ground, its walls, the
	 * exit stairs, a raised Upthrust wall and anything else built as level
	 * geometry. A character's capsule is not one, so a patch is never drawn on
	 * somebody's head; and a ground zone and a telegraph marker have no collision
	 * at all, so one drawing is never put on top of another.
	 *
	 * @return false, leaving `OutSurfaceZ` at `Point.Z`, when there is no world,
	 *         no such surface in the window -- every automation test world that
	 *         builds no geometry, and a point out over the void past a floor's
	 *         edge -- or when the search began inside a solid shape
	 */
	CATACLYSM_API bool FloorSurfaceUnder(const UWorld* World, const FVector& Point, float& OutSurfaceZ);

	/**
	 * `Point` moved straight up or down to `LiftCm` above the walking surface under
	 * it, or `Point` unchanged when `FloorSurfaceUnder` finds none.
	 *
	 * UNCHANGED RATHER THAN GUESSED WHEN THERE IS NO FLOOR. A drawing with nothing
	 * under it is drawn where it was asked for, which is every drawing in a test
	 * world with no geometry, so no test that measured a drawing before this
	 * measures a different one now.
	 *
	 * @param WorldContextObject  anything with a world: the zone, the caster, the
	 *                            world itself
	 */
	CATACLYSM_API FVector OnTheFloor(const UObject* WorldContextObject, const FVector& Point);
}
