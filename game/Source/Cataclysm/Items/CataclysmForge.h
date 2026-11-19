// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmItem.h"
#include "CataclysmForge.generated.h"

class UDataTable;

/**
 * What raising one item a gear level at the forge would take, or why it cannot. A plain struct, read at once by C++.
 */
struct FCataclysmForgeUpgrade
{
	/** Whether the upgrade can be made with what is held. False with `Refusal` saying why. */
	bool bPossible = false;

	/** Why not, in a sentence the screen shows. Empty when it is possible. */
	FString Refusal;

	/** The item's gear level now, and the level the upgrade takes it to. */
	int32 FromLevel = 0;
	int32 ToLevel = 0;

	/** The stone it consumes, a row of `game/Data/CraftingMaterials.csv`, "Material_Upgrade_Stone_3"; None at +10. */
	FName Stone;

	/** How many of that stone it consumes, and how many are held. */
	int32 StonesNeeded = 0;
	int32 StonesHeld = 0;

	/** The item's Cataclysmic Residue before the upgrade, and after it adds its own. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: one, and the residue's time penalty read before the upgrade adds its own. */
	int32 Days = 0;
};

/**
 * What combining two upgrade stones of one level into one of the next would take, or why it cannot.
 */
struct FCataclysmForgeCombine
{
	bool bPossible = false;
	FString Refusal;

	/** The level of the two stones combined, and of the one they make. */
	int32 FromLevel = 0;
	int32 IntoLevel = 0;

	/** The two stones' row and the made stone's row. */
	FName From;
	FName Into;

	/** How many of the lower stone it consumes, and how many are held. */
	int32 StonesNeeded = 0;
	int32 StonesHeld = 0;
};

/**
 * The Cataclysmic Forge's upgrade: raising an item one gear level with upgrade stones, and combining two stones of a
 * level into one of the next. The owner's ruling of 2026-11-19 ("build it next"), planned and ruled by the coordinating
 * session the same day. `docs/Cataclysm_GDD_v2.md`, "Gear Leveling" and section VII's operations table, whose row
 * reads "Upgrade Item Level | Upgrade Stone | 25 per tier | 1 per tier, maximum 10 | Consumes two upgrade stones".
 *
 * THE RULES ONLY, EVERY FUNCTION PURE: what an upgrade or a combine takes, from an item and a count of stones. Taking
 * the stones, changing the item, passing the days and saving are `UCataclysmForgeCraft`'s, in
 * `game/Source/Cataclysm/Player/CataclysmForgeCraft.h`, so a test reaches these with no world.
 *
 * WHAT THE DESIGN SETTLES, AND WHERE:
 *   - one level per craft, in order, +1 to +10: "Upgrades are sequential -- you cannot skip levels";
 *   - two stones of the level the item goes to: the operations row, the materials paragraph ("a +8 stone is what
 *     raises a piece to +8, and two of them are consumed") and the design workbook ("Uses two +x upgrade stones");
 *   - 25 residue per level: the workbook names the row "Upgrade Item Level (1 Tier)", "25 CR per tier", and
 *     `sim/cataclysm_sim/residue.py`'s `UPGRADE_LEVEL_RESIDUE_PER_LEVEL`, which the Consumption Thresholds are derived
 *     from, adds 25 per level raised;
 *   - the days: "1 day per tier" in the workbook, and the global time penalty, `CR / 100` rounded down, "The residue
 *     used is the item's own residue at the moment the craft begins, before the craft's own CR is added";
 *   - no failure: "A craft never destroys the item being crafted", and the forge is "deterministic".
 *
 * AND THE JUDGEMENTS, ruled 2026-11-19: combining costs no days and adds no residue, because it changes no item (the
 * operations table has no row for it, against "All capital services cost time"); a two-handed weapon takes two stones
 * as any item does (Diablo IV doubles material costs for one); no gold is charged in this slice.
 */
UCLASS()
class CATACLYSM_API UCataclysmForge : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many stones an upgrade consumes, and how many of a level a combine consumes. */
	static constexpr int32 StonesPerUpgrade = 2;
	static constexpr int32 StonesPerCombine = 2;

	/** The Cataclysmic Residue one level adds to the item. Mirrors `UPGRADE_LEVEL_RESIDUE_PER_LEVEL`. */
	static constexpr float ResiduePerLevel = 25.0f;

	/** The days one level costs before the residue's time penalty. */
	static constexpr int32 BaseDaysPerLevel = 1;

	/** The residue that adds one day to a craft: the design's `CR / 100`, rounded down. */
	static constexpr float ResiduePerPenaltyDay = 100.0f;

	/** The highest upgrade stone, +10, which is the highest gear level. */
	static constexpr int32 HighestStone = UCataclysmItemValues::MaxGearLevel;

	/** The row of the stone that raises an item to this level, "Material_Upgrade_Stone_3"; None outside 1 to 10. */
	static FName StoneFor(int32 Level);

	/** The days a craft on an item carrying this residue costs: one, and `Residue / 100` rounded down. */
	static int32 DaysFor(float Residue);

	/**
	 * What raising this item one gear level takes, against the stones of the next level held.
	 *
	 * REFUSED for no item, for an item at +10, and for fewer than two stones of the next level.
	 */
	static FCataclysmForgeUpgrade UpgradeOf(const FCataclysmItem& Item, int32 StonesOfTheNextLevelHeld);

	/**
	 * What combining two stones of this level into one of the next takes, against the stones of this level held.
	 *
	 * REFUSED for a level outside 1 to 9 -- +10 is the highest stone -- and for fewer than two held.
	 */
	static FCataclysmForgeCombine CombineOf(int32 Level, int32 StonesOfThisLevelHeld);

	/**
	 * What the first press of Upgrade says, before the second does it: "Press again to upgrade to +3: two +3 stones,
	 * residue 87 to 112, 1 day of the run."
	 */
	static FString UpgradeQuestion(const FCataclysmForgeUpgrade& Upgrade);

	/**
	 * The question asked before an upgrade of a worn piece takes Worn Residue to or past the tier's Consumption
	 * Threshold: the equip's question (`UCataclysmWearing::ThresholdQuestion`) with "Upgrading" for "Wearing", and the
	 * same consequence in the design's words. Ruled 2026-11-19, from "There is no path across the threshold that does
	 * not show the warning first."
	 */
	static FString ThresholdQuestion(const FString& ItemName, float WornResidueAfter, float Threshold);

	/**
	 * The name the player sees for an item: its whole name, or its base's name when it has none, or the row key. The
	 * forge's screen and lines, the item comparison's heading (`UCataclysmItemComparison::HeadingFor`) and the play
	 * loop's wear line (`FCataclysmPlayLoopLog::WoreLine`, said by `UCataclysmWearing::WearFromCarried`) all name a
	 * piece by it, so the three cannot call one piece by different names.
	 */
	static FString NameOf(const FCataclysmItem& Item, const UDataTable* BaseTable, const UDataTable* AffixTable);
};
