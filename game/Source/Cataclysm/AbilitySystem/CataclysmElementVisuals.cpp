// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmElementVisuals.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmElementVisuals::AssetPath =
	TEXT("/Game/Data/DT_ElementVisuals.DT_ElementVisuals");

namespace
{
	/**
	 * Kept alive deliberately. The table is loaded on the first hit of a session
	 * and every hit and every projectile afterwards reuses it.
	 *
	 * AddToRoot rather than a plain pointer, because nothing else references the
	 * asset -- effects are spawned from code, not placed in a level -- so
	 * garbage collection would otherwise be free to take it back and the next
	 * hit would pay the load again.
	 */
	TWeakObjectPtr<const UDataTable> CachedElementVisuals;
}

const UDataTable* UCataclysmElementVisuals::LoadTable()
{
	if (CachedElementVisuals.IsValid())
	{
		return CachedElementVisuals.Get();
	}

	const UDataTable* Table = LoadObject<UDataTable>(nullptr, AssetPath);
	if (Table)
	{
		const_cast<UDataTable*>(Table)->AddToRoot();
		CachedElementVisuals = Table;
	}
	return Table;
}

bool UCataclysmElementVisuals::ColoursFor(FName DamageType,
										  FLinearColor& OutPrimary,
										  FLinearColor& OutSecondary)
{
	// An untyped hit is a normal case, not a fault: player damage carries no
	// damage type because the enemy resists everything equally, so a type would
	// be choosing between copies of one number. See
	// UCataclysmDamageCalculation's note on the generic resistance.
	if (DamageType.IsNone())
	{
		return false;
	}

	const UDataTable* Table = LoadTable();
	if (!Table)
	{
		return false;
	}

	// The row key is the leaf of the damage type's tag -- `Element.Demonic`
	// gives a row named `Demonic` -- which is exactly what DamageType holds, so
	// no second lookup table stands between a hit and its colours.
	// Cataclysm.Data.ElementVisualsCarryTheDesignedValues pins that convention.
	const FCataclysmElementVisualRow* Row =
		Table->FindRow<FCataclysmElementVisualRow>(
			DamageType, TEXT("CataclysmElementVisuals"), /*bWarnIfMissing=*/false);
	if (!Row)
	{
		return false;
	}

	OutPrimary = Row->PrimaryColour;
	OutSecondary = Row->SecondaryColour;
	return true;
}
