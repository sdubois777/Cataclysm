// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;
class UStruct;

/**
 * Enchantment rows that were renamed, and the one place a loaded save's items
 * are pointed at the new names. Issue #1799, the owner's decision of
 * 2026-09-30: "build a name alias".
 *
 * WHY IT IS NEEDED. An enchantment's row name is the first 48 characters of its
 * sentence, and `FCataclysmRolledEnchantment` stores that name on a dropped item
 * as a `SaveGame` field. A reword that changes those characters renames the
 * row, and every saved item carrying the old name would find nothing, with no
 * error anywhere.
 *
 * WHAT IS CARRIED IS THE NAME ONLY. The item also stores where its roll landed
 * inside the row's range (`PositiveRoll`, `NegativeRoll`, 0 to 1), and that is
 * read against the new row's range, so a value can never lie outside it.
 *
 * ONE STEP, NEVER A CHAIN. Every old name maps straight to a row that exists.
 * A row renamed twice edits its earlier line's target rather than adding a
 * second hop. `tools/tests/test_every_enchantment_alias_points_at_a_current_row.py`
 * parses `Aliases()` and refuses a target that is not a current row, a target
 * that is also an old name, and an old name that is still a current row.
 */
class CATACLYSM_API FCataclysmEnchantmentRenames
{
public:
	/** Every renamed row: the name a saved item may hold, and the row it means now. */
	static const TMap<FName, FName>& Aliases();

	/** The row a stored name means now: its alias, or the name itself. */
	static FName Current(FName Stored);

	/**
	 * Point every rolled enchantment anywhere inside this object's `SaveGame`
	 * fields at its current row. Called by `FCataclysmSaveStorage::FromJson`
	 * after a record's fields are read, so every loaded record passes through it.
	 *
	 * A WALK OVER THE PROPERTIES rather than a list of the fields that hold
	 * items, so a record shape added later is covered without an edit here: it
	 * goes into nested structs and into arrays of structs.
	 *
	 * @return how many names were changed, so a test can tell "renamed" from
	 *         "found nothing to rename".
	 */
	static int32 RenameIn(UObject* Object);

	/** The same walk over one struct's memory. */
	static int32 RenameInStruct(const UStruct* Struct, void* Memory);
};
