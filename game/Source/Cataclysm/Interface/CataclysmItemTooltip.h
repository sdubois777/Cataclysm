// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmItemTooltip.generated.h"

struct FCataclysmCarriedSlot;
struct FCataclysmItem;
struct FCataclysmRolledAffix;
class UDataTable;

/**
 * What a carried item says about itself, as lines of text.
 *
 * WHY THIS EXISTS. Issue #733. A cell in the carried inventory shows the item
 * base's own name and nothing else -- `Circlet`, `Jerkin`, `Striders`. Everything
 * that makes one Circlet different from another is invisible: its rarity, its
 * upgrade level, its four affixes with their tiers and rolls, its sockets and its
 * Cataclysmic Residue.
 *
 * That matters because of a rule the design makes on purpose. The Storage
 * section of `docs/Cataclysm_GDD_v2.md` says an item that will not fit "stays on
 * the floor" and the player decides what is worth a slot. **A player cannot make
 * that decision from what the grid shows.**
 *
 * WHY IT IS A SEPARATE CLASS FROM THE WIDGET, which is the same reason
 * `UCataclysmInventoryScreen` is separate from `UCataclysmInventoryWidget` and
 * `UCataclysmCombatOverlay` is separate from `ACataclysmHUD`. The automation test
 * command in `tools/unreal_build.py` passes `-nullrhi`, so nothing that reaches
 * the screen can be watched by a test. Everything here is a static function over
 * plain values, so every wording rule below is covered while the drawing stays
 * uncovered.
 *
 * WHAT DECIDES WHERE THE PANEL GOES: NOTHING HERE, AND THAT IS NOT AN OMISSION.
 * Issue #733 asks what to do when the cursor is near a screen edge and how wide
 * the panel is, because when it was written the screen was drawn on a canvas and
 * had to work both out by hand. The port to Slate in issue #735 deleted
 * `CellRectFor` and `PanelCoversPoint` along with the rest of that arithmetic.
 * A Slate tool tip places itself, follows the cursor, keeps itself on screen and
 * knows which widget it belongs to, so the only thing left to decide is what it
 * says. That is this file.
 *
 * WHAT IS NOT HERE. Comparing the hovered item against what is worn, which is
 * what Diablo and Last Epoch both put beside the tool tip. Equipment now exists
 * (issue #828) so it has become possible; it is issue #832 rather than this one,
 * because a comparison needs a second column and a rule for what "better" means.
 */
UCLASS()
class CATACLYSM_API UCataclysmItemTooltip : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * A character sheet stat name as a player reads it.
	 *
	 * `max_health` becomes `maximum health`, `crit_chance` becomes `critical
	 * strike chance`.
	 *
	 * WHY A TRANSFORMATION RATHER THAN A TABLE OF DISPLAY NAMES. There is no
	 * such table. `game/Data/Attributes.csv` says which primary attribute drives
	 * which stat and carries no wording, and adding a column would mean the
	 * workbook, the generator and the importer all changing for twenty short
	 * phrases. The whole transformation is underscores to spaces plus four word
	 * expansions, and
	 * `Cataclysm.Tooltip.EveryStatInTheDataReadsAsWords` checks the result
	 * against every stat that actually appears in `game/Data/ItemBases.csv` and
	 * `game/Data/Affixes.csv`, so a stat added later that this reads badly is a
	 * test failure rather than a surprise on screen.
	 */
	static FString StatInWords(const FString& Stat);

	/**
	 * How one affix reads, including its tier.
	 *
	 * THE FOUR SHAPES, AND THE FOURTH IS WHY THIS DOES NOT JUST FORMAT A NUMBER.
	 * `game/Data/Affixes.csv` holds 85 rows in four kinds and they do not all
	 * carry the same fields:
	 *
	 * | Kind | Rows | What it looks like |
	 * | :-- | --: | :-- |
	 * | Stat, flat | 20 | `+120 to maximum health (tier 7)` |
	 * | Stat, increased | 38 | `12% increased maximum health (tier 7)` |
	 * | Resistance | 3 | `+45 to Fire resistance (tier 7)` |
	 * | Ailment and Hybrid | 24 | falls back to the sheet's own phrase |
	 *
	 * THE FLAT AND INCREASED WORDING FOLLOWS PATH OF EXILE, which writes
	 * `+120 to maximum Life` and `12% increased maximum Life`. Last Epoch and
	 * Diablo IV both use the same two shapes. It is the convention a player
	 * coming from any of them already reads without being taught, and the
	 * project has no reason to differ.
	 *
	 * THE FALLBACK IS NOT A PLACEHOLDER. An Ailment affix carries no value kind
	 * at all and a Hybrid one grants two stats named in other columns, so
	 * neither has a "+N to X" shape to be put into. Both name themselves clearly
	 * in the sheet -- `Chance to bleed`, `Health and armor` -- so the line is
	 * that phrase and the number. `Cataclysm.Tooltip.EveryAffixInTheDataReads`
	 * walks all 85 rows and fails on any that produces nothing, which is what
	 * stops a shape added later being silently blank.
	 *
	 * @return an empty string when the affix is not in the table, since an affix
	 *         that cannot be looked up has nothing to say
	 */
	static FString AffixLine(const FCataclysmRolledAffix& Rolled,
							 const FCataclysmItem& Item,
							 const UDataTable* BaseTable,
							 const UDataTable* AffixTable);

	/**
	 * How one of a base's implicit stats reads.
	 *
	 * An implicit does not roll, so it has no tier and no band. It does scale
	 * with the piece's upgrade level, so a +5 helm states a bigger number than a
	 * +0 one and the tool tip has to say the number this piece actually has.
	 *
	 * @return an empty string when the stat name is empty, which is how a base
	 *         with only one implicit says so
	 */
	static FString ImplicitLine(const FString& Stat, const FString& Kind,
								float StatedValue, int32 GearLevel,
								bool bTwoHanded);

	/**
	 * The lines describing what a weapon is, and nothing for anything else.
	 *
	 * How many hands it takes and which weapon type it is, its sub type, how
	 * fast it swings, the damage types it carries and how many it could ever
	 * carry. Issue #856.
	 *
	 * SEPARATE FROM LinesFor SO IT CAN BE ASKED DIRECTLY, because each of
	 * these has a rule behind it worth a test of its own -- most of all that a
	 * Shield states no swing rate, because a shield is not swung.
	 *
	 * @return empty for anything that is not a weapon, which is every base
	 *         whose WeaponType column is blank
	 */
	static TArray<FString> WeaponLines(const FCataclysmItem& Item,
									   const UDataTable* BaseTable);

	/**
	 * Every line describing what a carried slot holds, in reading order.
	 *
	 * An empty slot gives no lines. A crafting material gives its name, how many
	 * are stacked and what it is for. An item gives its whole name, its upgrade
	 * level, what it is if it is a weapon, its implicits, its affixes, its
	 * sockets and its residue.
	 */
	static TArray<FString> LinesFor(const FCataclysmCarriedSlot& Slot,
									const UDataTable* BaseTable,
									const UDataTable* AffixTable,
									const UDataTable* CraftingMaterialTable);

	/** The same lines joined with newlines, which is what a tool tip takes. */
	static FString TextFor(const FCataclysmCarriedSlot& Slot,
						   const UDataTable* BaseTable,
						   const UDataTable* AffixTable,
						   const UDataTable* CraftingMaterialTable);

	/**
	 * A number as a tool tip states it: no decimal point when it is whole.
	 *
	 * `120` rather than `120.0`, and `2.5` rather than `2.500000`. Affix values
	 * come out of a tier band and a roll, so most are not whole and a few are.
	 */
	static FString NumberInWords(float Value);
};
