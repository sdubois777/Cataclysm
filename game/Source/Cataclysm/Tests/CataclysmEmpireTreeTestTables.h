// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"

/**
 * Empire tree tables made for a test. Issue #47, slice F6.
 */
namespace CataclysmEmpireTreeTestTables
{
	/**
	 * A copy of `Nodes` in which option `Option` (1 to 3) of `Node` waits on `Reason`.
	 *
	 * WHY: SINCE #47 SLICE F6 NO CENTRE MILESTONE OPTION WAITS ON A SYSTEM. The Soul Forge and The Master Tinkerer were
	 * the last two, waiting on crafting. The refusal for an option that waits (`UCataclysmEmpireTree::
	 * RefusalForChoosingOption`, and Imperial Prowess's J4) is still in the code, so the tests that showed it with The
	 * Soul Forge show it on this copy, with the reason the generated table used to hold.
	 */
	inline UDataTable* WithAnOptionWaiting(const UDataTable* Nodes, FName Node, int32 Option, const FString& Reason)
	{
		UDataTable* Copy = NewObject<UDataTable>(GetTransientPackage());
		Copy->RowStruct = FCataclysmEmpireTreeNodeRow::StaticStruct();
		for (const TPair<FName, uint8*>& Pair : Nodes->GetRowMap())
		{
			FCataclysmEmpireTreeNodeRow Row = *reinterpret_cast<const FCataclysmEmpireTreeNodeRow*>(Pair.Value);
			if (Pair.Key == Node)
			{
				(Option == 1 ? Row.Option1WaitsOn : (Option == 2 ? Row.Option2WaitsOn : Row.Option3WaitsOn)) = Reason;
			}
			Copy->AddRow(Pair.Key, Row);
		}
		return Copy;
	}

	/** The reason The Soul Forge's row held before #47 slice F6, word for word. */
	inline const TCHAR* CraftingReason()
	{
		return TEXT("crafting, which the game does not have yet (issue #47)");
	}
}

#endif // WITH_AUTOMATION_TESTS
