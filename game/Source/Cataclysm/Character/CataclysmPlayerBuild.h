// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

/**
 * `FCataclysmPlayerBuild` is DEFINED IN `Save/CataclysmSaveRecords.h` since #2688's slices 2b and 2c, because the
 * character record holds one (the build captured at the crossing, O4) and that header cannot include this one: this
 * header needed `FCataclysmWornItem` from it. Its three functions are still defined in `CataclysmPlayerBuild.cpp`.
 * This header is kept so the includes that name it keep working.
 */
#include "CoreMinimal.h"
#include "Save/CataclysmSaveRecords.h"
