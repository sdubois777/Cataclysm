// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Items/CataclysmItem.h"
#include "CataclysmGems.generated.h"

struct FCataclysmGemRow;

/**
 * What a gem gives the character it is socketed into. Issue #46, slice G1.
 *
 * THE RULINGS, the coordinating session's of 2026-11-21, each from the design document, a gem's own text or the
 * owner's precedent in #2815:
 *
 *   - AN AILMENT GEM IS A FLAT CHANCE on the ailment's chance stat, as the ailment affixes are
 *     (`UCataclysmItemModifiers::ModifiersFor` adds `FCataclysmAilmentKind::Stat` as Flat). Of The Viper's 0.2 is +20
 *     `poison_chance`.
 *   - EVERY OTHER GEM IS AN INCREASE, by its text: each says "Increases ...", Of Hatred's crit multiplier included.
 *     Never More, although `ECataclysmModifierSource::Gem` may grant it: no gem's text says "more".
 *   - THE SHEET STATES A FRACTION AND THE PIPELINE PERCENTAGE POINTS, so a sheet value is multiplied by 100. #2803
 *     ruled the same for Of The Scholar: "the sheet's 0.10 is 10 in the stat pipeline".
 *   - ONE EFFECT PER GEM, whatever slot it is in, and A TWO-HANDED WEAPON DOES NOT DOUBLE ITS GEMS: its 6 sockets
 *     already equal a pair's 3 + 3. So nothing here asks what the gem is socketed into.
 *
 * WHICH STAT AND WHICH BUCKET A GEM NAMES is data: the Stat and ValueKind columns the Gems sheet gains in this
 * slice's workbook stage, carried on `FCataclysmGemRow`. Until then these functions take the stat and the kind as
 * arguments, which is also how the tests drive them. `tools/tests/test_every_gem_names_a_stat_the_game_reads.py`
 * holds the ruled table of all 28.
 *
 * WHAT IS NOT HERE: putting a gem in a socket, or a socket carrying a gem (slices G3 and G4). Nothing calls these yet.
 */
UCLASS()
class CATACLYSM_API UCataclysmGems : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** What one unit of a sheet value is in the stat pipeline: the sheet's 0.2 is 20 percentage points. */
	static constexpr float PointsPerSheetUnit = 100.0f;

	/**
	 * The gem's sheet value at `Rarity`, a fraction: Everyday is the figure in its effect text, which the generator
	 * extracts, and Quality to Cataclysmic are the sheet's seven numeric columns.
	 */
	static float SheetValueAt(const FCataclysmGemRow& Row, ECataclysmRarity Rarity);

	/**
	 * The bucket a gem's ValueKind names: "flat" or "increased", in any case. False for anything else, "more"
	 * included, since no gem's text says "more" (ruled 2026-11-21).
	 */
	static bool BucketNamed(const FString& ValueKind, ECataclysmStatBucket& OutBucket);

	/**
	 * Adds a gem's modifier to `Totals`, keyed by stat as `UCataclysmItemModifiers::ModifiersFor` keys an item's: `SheetValue`
	 * times `PointsPerSheetUnit`, in the bucket `ValueKind` names, with source `ECataclysmModifierSource::Gem`.
	 *
	 * ADDS NOTHING AND RETURNS FALSE for no stat, a kind `BucketNamed` refuses, or a value not above nought: a gem
	 * that names nothing must not reach the character as a modifier of nought on a stat called "None".
	 */
	static bool AddModifier(FName Stat, const FString& ValueKind, float SheetValue,
							TMap<FName, TArray<FCataclysmStatModifier>>& Totals);
};
