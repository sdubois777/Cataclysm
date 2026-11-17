// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * The primary game module's log category.
 *
 * Used where a rule cannot be enforced at runtime and something has to be
 * ignored or clamped instead: the stat pipeline refusing a "more" multiplier
 * from a gear affix, for example. Those cases are counted in the value the
 * caller gets back as well, so a test can prove one happened without reading
 * the log, but the log is what tells a designer which piece of data was wrong.
 */
CATACLYSM_API DECLARE_LOG_CATEGORY_EXTERN(LogCataclysm, Log, All);
