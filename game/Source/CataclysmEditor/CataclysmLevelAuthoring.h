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
};
