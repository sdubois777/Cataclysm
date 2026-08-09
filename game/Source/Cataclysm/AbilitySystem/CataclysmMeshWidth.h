// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"

/**
 * Sizing a mesh to the width the game wants it to be.
 *
 * WHY THIS IS SHARED RATHER THAN WRITTEN WHERE IT IS NEEDED. Three things in the
 * project draw an art-pack mesh at a size the game chose rather than the size it
 * was authored at: a projectile's flying body, the pieces a debris burst
 * scatters, and the rock the Brute carries in its hand. Two of them had their
 * own copy of the arithmetic and the third had none at all, which is issue #453:
 * the rock in the hand drew at its authored 206.6 cm across while the same rock
 * in the air drew at 80, so the creature visibly threw a smaller rock than the
 * one it was holding.
 *
 * FROM THE MESH'S OWN BOUNDS, not from an assumption about how big it is. The
 * engine's basic shapes occupy a 100 centimetre cube and an art-pack mesh does
 * not, so a scale worked out for one is meaningless for the other. Measured
 * 2026-08-09 with tools/measure_rock_sizes.py: SM_Rock_To_Hold is authored
 * 206.6 x 180.9 x 512.2 cm, against a Brute whose capsule is 96 cm across and
 * 220 cm tall.
 *
 * THE HORIZONTAL HALF-EXTENT, not the bounding sphere radius. A bounding sphere
 * takes in the corners of the box, so scaling by it would leave the engine's own
 * sphere -- whose box is 50 cm each way and whose bounding sphere is 86.6 --
 * noticeably smaller than the width asked for. The two horizontal axes are what
 * the width of a flying object means.
 */
namespace CataclysmMeshWidth
{
	/**
	 * The uniform scale that makes `Mesh` come out `RadiusCm` wide about its
	 * centre, so twice that across.
	 *
	 * @return the scale, or 0 when there is nothing to scale: a null mesh, a
	 *         mesh with no width, or a radius that is not positive. **Callers
	 *         must leave the component alone on 0** rather than applying it,
	 *         which would collapse the mesh to nothing.
	 */
	inline float ScaleFor(const UStaticMesh* Mesh, float RadiusCm)
	{
		if (!Mesh || RadiusCm <= 0.0f)
		{
			return 0.0f;
		}

		const FVector Extent = Mesh->GetBounds().BoxExtent;
		const float HalfWidth = FMath::Max(Extent.X, Extent.Y);
		if (HalfWidth <= UE_SMALL_NUMBER)
		{
			// A mesh with no width cannot be scaled to one. Answering zero
			// rather than dividing by it, which would be a scale of infinity.
			return 0.0f;
		}

		return RadiusCm / HalfWidth;
	}
}
