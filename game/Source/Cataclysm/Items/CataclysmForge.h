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
 * What combining two gems of one kind and rarity into one of the next rarity would take, or why it cannot. Issue #46,
 * slice G6.
 */
struct FCataclysmForgeGemCombine
{
	bool bPossible = false;
	FString Refusal;

	/** The gem, the rarity of the two combined, and the rarity of the one they make. */
	FName Gem;
	ECataclysmRarity From = ECataclysmRarity::Everyday;
	ECataclysmRarity Into = ECataclysmRarity::Everyday;

	/** How many of the lower gem it consumes, and how many are held. */
	int32 GemsNeeded = 0;
	int32 GemsHeld = 0;
};

/**
 * What putting a gem into a socket, or taking one out, would take, or why it cannot. Issue #46, slice G4.
 */
struct FCataclysmForgeSocketing
{
	bool bPossible = false;
	FString Refusal;

	/** False to socket a gem, true to take one out. */
	bool bUnsocket = false;

	/** Which socket, from 0, and its type. */
	int32 Socket = INDEX_NONE;
	ECataclysmSocketType SocketType = ECataclysmSocketType::Any;

	/** The gem put in or taken out, and its rarity. */
	FName Gem;
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;

	/** The material it consumes, one of it, and how many are held. */
	FName Material;
	int32 MaterialHeld = 0;

	/** The item's residue before the craft, and after it adds its own. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: one, and the residue's time penalty read before the craft adds its own. */
	int32 Days = 0;
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

	//~ Combining gems. Issue #46, slice G6.

	/**
	 * How many gems of a kind and rarity one combine consumes: the stone combine's own figure, read from it rather than
	 * copied, since the owner ruled on 2026-11-21 that combining gems "should probably work the same way as gear
	 * leveling".
	 */
	static constexpr int32 GemsPerCombine = StonesPerCombine;

	/**
	 * What combining two gems of `Gem` at `Rarity` into one of the next rarity takes, against those held.
	 *
	 * REFUSED for no gem, for a Cataclysmic gem -- the highest rarity -- and for fewer than two held. NO CAP BY THE
	 * DIFFICULTY TIER, as the stone combine has none: the owner's ruling of 2026-11-21, "I don't think I want to cap what
	 * players can level their stuff to". AND NO DAY, NO RESIDUE AND NO GOLD, as the stone combine costs none of them: it
	 * changes no item (the design's "Gear Leveling" paragraph), and no craft has a gold price yet.
	 */
	static FCataclysmForgeGemCombine GemCombineOf(FName Gem, ECataclysmRarity Rarity, int32 HeldOfThis);

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

	//~ Socketing and unsocketing a gem. Issue #46, slice G4, ruled by the coordinating session on 2026-11-21.
	//
	// THE DESIGN'S OPERATIONS ROWS, section VII: "Socket Gem | Jeweler's Setting Agent | 5 | 1" and "Unsocket Gem |
	// Dismantling Dust | 3 | 1", the residue added and the base days, with the same time penalty as every craft. No free
	// removal: a gem comes out only with Dismantling Dust ("Safe unsocketing"), and comes back to the bag. A gem fits a
	// socket of its own type, or a socket for any gem.

	/** The row of the material Socket Gem consumes, one each: `game/Data/CraftingMaterials.csv`. */
	static const TCHAR* SettingAgentRow;

	/** The row of the material Unsocket Gem consumes, one each. */
	static const TCHAR* DismantlingDustRow;

	/** The residue Socket Gem adds. Mirrors `SOCKET_GEM_RESIDUE` in `sim/cataclysm_sim/residue.py`. */
	static constexpr float SocketGemResidue = 5.0f;

	/** The residue Unsocket Gem adds. */
	static constexpr float UnsocketGemResidue = 3.0f;

	/** The days each costs before the residue's time penalty. */
	static constexpr int32 SocketGemBaseDays = 1;
	static constexpr int32 UnsocketGemBaseDays = 1;

	/** The days a craft of `BaseDays` costs on an item carrying this residue: `BaseDays` and `Residue / 100`, down. */
	static int32 DaysFor(float Residue, int32 BaseDays);

	/**
	 * What putting `Gem` at `Rarity`, a gem of `GemType`, into the item's socket `Socket` takes, against the gems of that
	 * kind and the Setting Agents held.
	 *
	 * REFUSED for no item, no such socket, a socket that holds a gem, no such gem carried, a gem of another type than a
	 * typed socket's, and no Setting Agent.
	 */
	static FCataclysmForgeSocketing SocketGemOf(const FCataclysmItem& Item, int32 Socket, FName Gem,
												ECataclysmRarity Rarity, const FString& GemType, int32 GemsHeld,
												int32 AgentsHeld);

	/**
	 * What taking the gem out of the item's socket `Socket` takes, against the Dismantling Dust held and whether the bag
	 * has room for the gem (a stack of it, a free cell, or the cell the last Dust frees).
	 *
	 * REFUSED for no item, no such socket, an empty socket, no Dust, and no room.
	 */
	static FCataclysmForgeSocketing UnsocketGemOf(const FCataclysmItem& Item, int32 Socket, int32 DustHeld,
												  bool bRoomForTheGem);

	/**
	 * What the first press of a socketing says, before the second does it: "Press again to socket Of The Viper (Superb)
	 * into the Attack socket: one Jeweler's Setting Agent, residue 87 to 92, 1 day of the run." or "Press again to
	 * unsocket Of The Viper (Superb): one Dismantling Dust, residue 87 to 90, 1 day of the run; the gem goes back to the
	 * bag."
	 */
	static FString SocketingQuestion(const FCataclysmForgeSocketing& Plan);
};
