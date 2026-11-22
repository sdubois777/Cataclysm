// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Items/CataclysmItem.h"
#include "CataclysmGems.generated.h"

class UAbilitySystemComponent;

struct FCataclysmGemRow;
class UDataTable;

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
 * GEMS AS ITEMS, slice G2: a gem is carried as a stack of one gem at one rarity (`FCataclysmCarriedSlot::Gem`), and
 * its name, its effect at that rarity and its tooltip are read from `DT_Gems` here.
 *
 * WHAT IS NOT HERE: putting a gem in a socket, or a socket carrying a gem (slices G3 and G4). Nothing calls
 * `AddModifier` yet.
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

	//~ Gems as items. Issue #46, slice G2.

	/** Where the generated gem table lives: `DT_Gems`, from `game/Data/Gems.csv`. */
	static const TCHAR* TableAssetPath;

	/** The generated gem table, or null with the reason logged. */
	static const UDataTable* LoadGeneratedTable();

	/** A gem's row in `Table`, or null for no table or no such gem. */
	static const FCataclysmGemRow* RowOf(const UDataTable* Table, FName Gem);

	/** What the player reads for a gem: its GemName, "Of The Viper", or the row key when the table has no such row. */
	static FString NameOf(FName Gem);

	/** A rarity's name as the player reads it: "Superb". */
	static FString RarityName(ECataclysmRarity Rarity);

	/**
	 * The gem's effect text at `Rarity`: its Effect with the figure it states replaced by that rarity's value.
	 * "20% chance to apply poison" is "60% chance to apply poison" at Superb, and "Increases Magic Find by .5%" is
	 * "Increases Magic Find by 0.5%" at Everyday. The Everyday figure in the text is the one the generator extracts as
	 * the Everyday value, so the first figure before a "%" is the one replaced.
	 */
	static FString EffectAt(const FCataclysmGemRow& Row, ECataclysmRarity Rarity);

	/**
	 * A carried gem stack's tooltip, one line each: its name; "Superb Attack gem"; its effect at that rarity; which
	 * socket it fits ("Fits an Attack socket", ruled 2026-11-21: a gem fits a socket of its own type); and how many are
	 * carried, when more than one, as a material's tooltip says it. A gem the table lacks gets its row key and rarity.
	 *
	 * `Character`, since issue #2835: the character reading it, whose own amount of the gem's stat decides the
	 * nothing-to-increase line (`NothingToIncreaseLineFor`). Null keeps the line that is true for every character.
	 */
	static TArray<FString> TooltipLinesFor(FName Gem, ECataclysmRarity Rarity, int32 Quantity,
										   const UAbilitySystemComponent* Character = nullptr);

	/**
	 * The tooltip's line for a gem that increases a stat no class has a base of -- block chance, evasion, cooldown
	 * reduction or magic find -- and empty for any other gem. Ruled 2026-11-21, "keep and explain", the owner's own
	 * answer for attributes in #2815 ("Keep it, just explain it"); its form by the coordinating session on 2026-11-22: a
	 * fixed line chosen from the row's stat alone, true for every character, so it states the rule rather than the
	 * character's state. Gear is the only source of a flat amount of each of the four (affixes, and the Shield, the
	 * armour bases and the Reliquary); no passive gives one.
	 *
	 * FOR THE CHARACTER READING IT, since issue #2835 (the coordinating session's plan of 2026-11-22): given a
	 * `UCataclysmAbilitySystemComponent`, the line is "You have no evasion, from your class or your items, so this gem
	 * does nothing for you until your gear gives some." when `AmountToIncreaseFor` is 0 or less, and empty when it is
	 * more -- the effect line already says what the gem does then. Null, or another kind of component, gives the fixed
	 * line above.
	 */
	static FString NothingToIncreaseLineFor(const FCataclysmGemRow& Row,
											const UAbilitySystemComponent* Character = nullptr);

	/**
	 * What an increase of `Stat` multiplies for this character: its base plus its flat amount, the sum the character
	 * sheet's attribute tooltip reads ("NOTHING TO INCREASE", `UCataclysmCharacterSheetLayout`). From the stat's recorded
	 * inputs when it has any (`UCataclysmAbilitySystemComponent::GetStatInputs`), evaluated with no skill tags, so a flat
	 * amount that applies only under a condition does not count, as on the sheet. A stat with none recorded has no
	 * modifier at all, so its attribute's value is its base. 0 for no component, or a stat no attribute holds. Issue #2835.
	 */
	static float AmountToIncreaseFor(const UAbilitySystemComponent* Character, FName Stat);

	//~ Gems in sockets. Issue #46, slice G3.

	/** A socket type's name as the player reads it: "Attack", "Defense", "Utility", "Any". */
	static FString SocketTypeName(ECataclysmSocketType Type);

	/**
	 * Whether a gem of `GemType` ("Attack", "Defense", "Utility", the Gems sheet's column) fits a socket of `Type`.
	 * An `Any` socket fits every gem; a typed one fits a gem of its own type, ruled 2026-11-21.
	 */
	static bool GemFits(ECataclysmSocketType Type, const FString& GemType);

	/** A new socket's type, evenly among Attack, Defense and Utility; never `Any`. Ruled 2026-11-21. */
	static ECataclysmSocketType RollSocketType(FRandomStream& Stream);

	/**
	 * Which gem a gem drop is: evenly among the table's rows, in the table's own order, so one seed always gives one
	 * gem. None for a missing or empty table. Issue #46, slice G5, ruled 2026-11-21 ("uniform among the 28").
	 */
	static FName RollGem(const UDataTable* GemTable, FRandomStream& Stream);

	/** A gem's name with its rarity, as the floor and the forge show it: "Of Life (Superb)". Issue #46, slice G5. */
	static FString FloorNameOf(FName Gem, ECataclysmRarity Rarity);

	/**
	 * Adds the modifier of one gem at `Rarity` to `Totals`: `AddModifier` with the row's Stat, its ValueKind and its
	 * sheet value at that rarity. False, adding nothing, when the row names no stat or kind.
	 *
	 * IT READS `FCataclysmGemRow::Stat` AND `ValueKind`, which the Gems sheet's columns of the same names give in slice
	 * G1's workbook stage. This branch does not compile until it is rebased onto that stage.
	 */
	static bool AddGemModifier(const FCataclysmGemRow& Row, ECataclysmRarity Rarity,
							   TMap<FName, TArray<FCataclysmStatModifier>>& Totals);

	/**
	 * Adds the modifier of every gem in `Item`'s sockets to `Totals`. Called for each worn piece by
	 * `UCataclysmItemModifiers::AccumulateInto`, so a gem in a carried piece does nothing.
	 *
	 * ONCE PER GEM, WHATEVER THE PIECE: one effect per gem whatever it is socketed into, and a two-handed weapon does not
	 * double its gems, because its 6 sockets already equal a pair's 3 + 3 (both ruled 2026-11-21). A gem the table
	 * lacks is skipped and logged, rather than reaching the character as nothing under a wrong name.
	 */
	static void AddSocketedGems(const FCataclysmItem& Item, const UDataTable* GemTable,
								TMap<FName, TArray<FCataclysmStatModifier>>& Totals);

	/**
	 * One tooltip line per socket, in order: "Attack socket: empty", "Defense socket: Of The Titan (Superb)", and for a
	 * socket that fits any gem, "Socket for any gem: empty".
	 */
	static TArray<FString> SocketLinesFor(const FCataclysmItem& Item);
};
