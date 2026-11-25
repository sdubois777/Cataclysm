// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmEffectPlacement.h"

#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

bool CataclysmEffectPlacement::FloorSurfaceUnder(const UWorld* World, const FVector& Point,
												 float& OutSurfaceZ)
{
	OutSurfaceZ = static_cast<float>(Point.Z);
	if (!World)
	{
		return false;
	}

	// STRAIGHT DOWN THROUGH THE POINT, FROM ABOVE IT. See `LookAboveCm` for why the
	// search does not begin at the point itself.
	const FVector From(Point.X, Point.Y, Point.Z + LookAboveCm);
	const FVector To(Point.X, Point.Y, Point.Z - LookBelowCm);

	// BY OBJECT TYPE, as the charge's floor search in `ACataclysmEnemyCharacter`
	// asks: what IS world-static geometry, so no character's capsule answers.
	// Simple collision rather than complex: the floor's blocks are boxes, and the
	// box is the surface a character stands on.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CataclysmEffectPlacement),
								 /*bInTraceComplex=*/false);
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, From, To,
											FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return false;
	}

	// A SEARCH THAT BEGAN INSIDE A SOLID SHAPE HAS NOT FOUND A SURFACE, it has
	// found the shape it is in, at the height it started. Drawing there would put
	// a patch in the air inside a wall.
	if (Hit.bStartPenetrating)
	{
		return false;
	}

	OutSurfaceZ = static_cast<float>(Hit.ImpactPoint.Z);
	return true;
}

FVector CataclysmEffectPlacement::OnTheFloor(const UObject* WorldContextObject, const FVector& Point)
{
	const UWorld* World = (WorldContextObject && GEngine)
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	float SurfaceZ = 0.0f;
	if (!FloorSurfaceUnder(World, Point, SurfaceZ))
	{
		return Point;
	}
	return FVector(Point.X, Point.Y, static_cast<double>(SurfaceZ + LiftCm));
}
