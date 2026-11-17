// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmLevelAuthoring.generated.h"

class ANavMeshBoundsVolume;

/**
 * Level authoring steps that the editor's Python layer cannot perform on its own.
 *
 * WHY THIS EXISTS AT ALL. tools/generate_input_assets.py builds the sandbox level
 * from a script so the level is reproducible rather than clicked together. One
 * step is not reachable from Python: a navigation bounds volume gets its size
 * from a brush, brushes are made by UCubeBuilder, and UCubeBuilder is not
 * exposed to the editor scripting layer. Its Brush property is not readable from
 * there either, so a script cannot even check whether it worked.
 *
 * Without a sized volume there is no navigation mesh, click-to-move finds no
 * path, and clicking does nothing with no error reported anywhere.
 */
UCLASS()
class UCataclysmLevelAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Spawns a navigation bounds volume of the given size, built and ready.
	 *
	 * @param World  the level being authored
	 * @param Origin centre of the volume, in world space
	 * @param Size   full width, depth and height in centimetres, not half-extents
	 * @return the volume, or null if it could not be built
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Level Authoring")
	static ANavMeshBoundsVolume* AddNavMeshBounds(UWorld* World, FVector Origin, FVector Size);

	/**
	 * The volume's world-space bounding box extent, so a script can confirm the
	 * brush was actually built rather than assume it.
	 *
	 * Returns zero for a volume with no brush, which is exactly the failure this
	 * is here to make visible.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Level Authoring")
	static FVector GetVolumeExtent(const ANavMeshBoundsVolume* Volume);

	/**
	 * Builds the navigation mesh and does not return until it is finished.
	 *
	 * WHY A SCRIPT CANNOT DO THIS ITSELF. UNavigationSystemV1::Build is a plain
	 * virtual function, not a UFUNCTION, so the editor scripting layer cannot
	 * reach it. Neither is the lock this has to release first.
	 *
	 * WHY IT IS NEEDED AT ALL. Creating a navigation bounds volume does not build
	 * anything on its own inside a commandlet: loading the map asynchronously
	 * locks navigation building and nothing releases that lock, so the level is
	 * saved with a navigation mesh containing no data. Click-to-move then calls
	 * SimpleMoveToLocation, finds no path, and the character turns to face the
	 * point clicked and stops, while holding the button still works because that
	 * steers directly and never asks for a path.
	 *
	 * Setting the navigation data to generate at run time instead does not fix
	 * it. That only permits rebuilds; something must still mark an area dirty to
	 * trigger one, and on a level whose geometry never changes nothing does. That
	 * was measured on 2026-08-04, not assumed. Issue #142.
	 *
	 * @return true if the build ran
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Level Authoring")
	static bool BuildNavigation(UWorld* World);

	/**
	 * Whether a point can be placed on the navigation mesh.
	 *
	 * This is the check that matters, rather than counting tiles or reading
	 * bounds. Click-to-move fails with "start point not on navmesh", so asking
	 * whether a point is on the mesh tests the exact thing that breaks.
	 *
	 * @param World      the level being authored
	 * @param Point      world-space point to test, normally the middle of the floor
	 * @param SearchSize how far from the point to look, in centimetres
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Level Authoring")
	static bool IsPointOnNavMesh(UWorld* World, FVector Point, float SearchSize = 200.0f);
};
