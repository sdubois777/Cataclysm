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

	/** The days of the run it costs: the level it reaches, and the residue's time penalty read before it adds its own. */
	int32 Days = 0;

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
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

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
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

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
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

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
};

/**
 * What adding a socket to an item, or changing the type of one of its sockets, would take, or why it cannot. Issue #46,
 * slice G9.
 */
struct FCataclysmForgeSocketWork
{
	bool bPossible = false;
	FString Refusal;

	/** True to add a socket, false to change one's type. */
	bool bAdd = false;

	/** The socket changed, from 0; or, for an addition, the place the new socket takes, the item's socket count. */
	int32 Socket = INDEX_NONE;

	/** A change's socket type before and the type chosen. An addition's `ToType` is rolled when it is done. */
	ECataclysmSocketType FromType = ECataclysmSocketType::Any;
	ECataclysmSocketType ToType = ECataclysmSocketType::Any;

	/** The most sockets the item's slot takes, `game/Data/ItemSockets.csv`. Read for an addition. */
	int32 MaxSockets = 0;

	/** The material it consumes, one of it, and how many are held. */
	FName Material;
	int32 MaterialHeld = 0;

	/** The item's residue before the craft, and after it adds its own. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: its base days, and the residue's time penalty read before the craft. */
	int32 Days = 0;

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
};

/** The three crafts that change one affix: its roll drawn again, its roll made the top, or its tier raised. #47, F1. */
enum class ECataclysmForgeAffixCraft : uint8
{
	/** Reroll Affix Value: a Corrupted Mote draws the roll again, anywhere in the tier's range. */
	RerollValue,

	/** Perfect Roll Attempt: a Primal Spark makes the roll the top of the tier's range. */
	PerfectRoll,

	/** Upgrade Affix Tier: a Potency Crystal raises the tier by one, to 7, and keeps the roll. */
	UpgradeTier,
};

/**
 * What one of the three affix crafts would take on one affix of an item, or why it cannot. Issue #47, slice F1.
 */
struct FCataclysmForgeAffixWork
{
	bool bPossible = false;
	FString Refusal;

	/** Which of the three crafts. */
	ECataclysmForgeAffixCraft Craft = ECataclysmForgeAffixCraft::RerollValue;

	/** The affix changed, from 0, and its row. */
	int32 Affix = INDEX_NONE;
	FName AffixRow;

	/** Its tier before and after: the same, except an upgrade's, which is one higher. */
	int32 FromTier = 0;
	int32 ToTier = 0;

	/**
	 * Its roll before and after, a fraction of the tier's range. A reroll's `ToRoll` is drawn when it is done, so a plan
	 * holds the roll before in it; a perfecting's is 1; an upgrade's is the roll before, kept.
	 */
	float FromRoll = 0.0f;
	float ToRoll = 0.0f;

	/** The material it consumes, one of it, and how many are held. */
	FName Material;
	int32 MaterialHeld = 0;

	/** The item's residue before the craft, and after it adds its own. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: its base days, and the residue's time penalty read before the craft. */
	int32 Days = 0;

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
};

/**
 * What adding a chosen affix to an item, or shifting one of its affixes to another stat, would take, or why it cannot.
 * Issue #47, slice F2.
 */
struct FCataclysmForgeAffixChange
{
	bool bPossible = false;
	FString Refusal;

	/** True to shift an affix's stat (Affix Shifting), false to add a chosen affix (Deterministic Affix). */
	bool bShift = false;

	/** The affix changed, from 0; for an addition, the place the new affix takes, the item's affix count. */
	int32 Affix = INDEX_NONE;

	/** A shift's affix row before it; None for an addition. */
	FName FromRow;

	/** The affix row after: the one chosen for an addition, the one drawn for a shift. */
	FName ToRow;

	/** The damage types a resistance affix covers after, drawn as a drop draws them; empty for every other kind. */
	TArray<FName> ToDamageTypes;

	/** The tier and roll after: a shift keeps both; an addition takes the tier chosen and a roll drawn in it. */
	int32 Tier = 0;
	float Roll = 0.0f;

	/** The piece's rarity before and after. An addition promotes it one rarity; a piece with no affix has none before. */
	bool bHadRarity = false;
	ECataclysmRarity FromRarity = ECataclysmRarity::Everyday;
	ECataclysmRarity ToRarity = ECataclysmRarity::Everyday;

	/** The material it consumes, one of it, and how many are held. */
	FName Material;
	int32 MaterialHeld = 0;

	/** The item's residue before the craft, and after it adds its own. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: its base days, and the residue's time penalty read before the craft. */
	int32 Days = 0;

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
};

/** The three crafts that change a weapon's damage types. Issue #47, slice F4. */
enum class ECataclysmForgeDamageTypeCraft : uint8
{
	/** Infuse Random Damage Type: an Elemental Cinder adds a damage type drawn among those the weapon can take. */
	InfuseRandom,

	/** Infuse Specific Damage Type: a Sovereign Ingot adds the damage type the player chooses. */
	InfuseSpecific,

	/** Change Damage Type: a Prismatic Catalyst replaces a damage type the player chooses with one drawn. */
	Change,
};

/**
 * What one of the three damage-type crafts would take on a weapon, or why it cannot. Issue #47, slice F4.
 */
struct FCataclysmForgeDamageTypeWork
{
	bool bPossible = false;
	FString Refusal;

	/** Which of the three crafts. */
	ECataclysmForgeDamageTypeCraft Craft = ECataclysmForgeDamageTypeCraft::InfuseRandom;

	/** A change's damage type replaced, the one the player chose; None for an infusion. */
	FName FromType;

	/** The damage type added or put in its place: chosen for a specific infusion, drawn for the other two. */
	FName ToType;

	/** How many damage types the weapon carries before, and the most it may, `UCataclysmDropRoll::MaxDamageTypesFor`. */
	int32 TypesHeld = 0;
	int32 MaxTypes = 0;

	/** The material it consumes, one of it, and how many are held. */
	FName Material;
	int32 MaterialHeld = 0;

	/** The item's residue before the craft, and after it adds its own. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: its base days, and the residue's time penalty read before the craft. */
	int32 Days = 0;

	/** The gold it costs at the difficulty tier of the run, and the gold held. Issue #2830. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
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

	// THE DAYS AN UPGRADE COSTS BEFORE THE RESIDUE'S TIME PENALTY ARE THE LEVEL IT REACHES: the owner's ruling of
	// 2026-11-21, word for word, "+0 to +10 takes 55 days per item (1+2+...+10)", reading the Crafting sheet's "1 day per
	// tier. (Max of 10)". Until that ruling it was one day a level (`BaseDaysPerLevel`, deleted). Issue #2830.

	/** The residue that adds one day to a craft: the design's `CR / 100`, rounded down. */
	static constexpr float ResiduePerPenaltyDay = 100.0f;

	/** The highest upgrade stone, +10, which is the highest gear level. */
	static constexpr int32 HighestStone = UCataclysmItemValues::MaxGearLevel;

	/** The row of the stone that raises an item to this level, "Material_Upgrade_Stone_3"; None outside 1 to 10. */
	static FName StoneFor(int32 Level);

	//~ Gold. Issue #2830 (#46, slice G7).

	/**
	 * The gold one base day of a craft costs at difficulty tier 1. A JUDGEMENT of the coordinating session, 2026-11-21,
	 * under the owner's delegation of numbers: about a third of one walk day's gold by the only income estimate there is
	 * (#2742, 7,000 to 14,000 gold for a 12-floor tier 1 walk; nothing measured). An operation's base gold is this times
	 * its base days, the Crafting sheet's own ordering of what is heavy.
	 */
	static constexpr int32 GoldPerCraftDay = 250;

	/** The residue that adds one more base price: the Crafting sheet's gold multiplier, `(CR / 50) + 1`. */
	static constexpr float ResiduePerGoldStep = 50.0f;

	/**
	 * What combining costs at the lowest rung, at tier 1: an Everyday gem or a +1 stone. Each rung above doubles it, for
	 * stones and gems alike, and no day passes. The owner's answer of 2026-11-21, "Gold only (Recommended)".
	 */
	static constexpr int32 CombineGoldAtTheLowestRung = 50;

	/** An operation's base gold: `GoldPerCraftDay` for each base day, and one day's price when it has none or fewer. */
	static int32 BaseGoldFor(int32 BaseDays);

	/**
	 * What a craft of `BaseGold` costs on an item carrying `Residue`, at a difficulty tier: the base, times the tier's
	 * gold scale (`UCataclysmDropRoll::GoldScaleFor`, which every gold price reads), times `(Residue / 50) + 1`.
	 * THE MULTIPLIER IS NOT ROUNDED; only the gold figure is, to whole gold, at least 1. Ruled 2026-11-21.
	 */
	static int64 GoldFor(int32 BaseGold, float Residue, int32 DifficultyTier);

	/**
	 * What one combine costs at a rung, at a difficulty tier: `CombineGoldAtTheLowestRung` doubled for each rung above
	 * the lowest, times the tier's gold scale. No residue enters: no item is involved. THE ONE FUNCTION BOTH COMBINES
	 * READ, the stone combine at rung `Level - 1` and the gem combine at the rarity's rung.
	 */
	static int64 CombineGoldFor(int32 Rung, int32 DifficultyTier);

	/** A refusal for too little gold: "Raising it to +3 takes 2,055 gold; 900 gold held." */
	static FString GoldRefusal(const FString& What, int64 Gold, int64 Held);

	//~ The Crafting sheet's operations table, `DT_ForgeOperations`, from `game/Data/ForgeOperations.csv` (#2830).

	static const TCHAR* OperationsTableAssetPath;
	static const TCHAR* UpgradeOperationRow;
	static const TCHAR* SocketGemOperationRow;
	static const TCHAR* UnsocketGemOperationRow;
	static const TCHAR* CombineOperationRow;

	/** The operations table, or null with the reason logged. */
	static const UDataTable* LoadOperationsTable();

	/**
	 * An operation's Base Gold from the table, the figure the owner sets in the sheet. `IfMissing` -- the constant the
	 * sheet's figure mirrors -- only when the table or the row is missing, with the reason logged, so a missing asset
	 * prices the forge as the sheet did when it was last checked rather than at nothing.
	 */
	static int32 BaseGoldOf(const TCHAR* Operation, int32 IfMissing);

	/**
	 * What raising this item one gear level takes, against the stones of the next level and the gold held, at the
	 * difficulty tier of the run.
	 *
	 * REFUSED for no item, for an item at +10, for fewer than two stones of the next level, and for too little gold.
	 */
	static FCataclysmForgeUpgrade UpgradeOf(const FCataclysmItem& Item, int32 StonesOfTheNextLevelHeld,
											int32 DifficultyTier, int64 GoldHeld);

	/**
	 * What combining two stones of this level into one of the next takes, against the stones of this level held.
	 *
	 * REFUSED for a level outside 1 to 9 -- +10 is the highest stone -- for fewer than two held, and for too little
	 * gold (`CombineGoldFor` at rung `Level - 1`).
	 */
	static FCataclysmForgeCombine CombineOf(int32 Level, int32 StonesOfThisLevelHeld, int32 DifficultyTier,
											int64 GoldHeld);

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
	 * REFUSED for no gem, for a Cataclysmic gem -- the highest rarity -- for fewer than two held, and for too little
	 * gold. NO CAP BY THE DIFFICULTY TIER, as the stone combine has none: the owner's ruling of 2026-11-21, "I don't think
	 * I want to cap what players can level their stuff to". NO DAY AND NO RESIDUE: it changes no item. ITS GOLD IS THE
	 * STONE COMBINE'S, `CombineGoldFor` at the rarity's rung (#2830).
	 */
	static FCataclysmForgeGemCombine GemCombineOf(FName Gem, ECataclysmRarity Rarity, int32 HeldOfThis,
												  int32 DifficultyTier, int64 GoldHeld);

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
	 * typed socket's, no Setting Agent, and too little gold.
	 */
	static FCataclysmForgeSocketing SocketGemOf(const FCataclysmItem& Item, int32 Socket, FName Gem,
												ECataclysmRarity Rarity, const FString& GemType, int32 GemsHeld,
												int32 AgentsHeld, int32 DifficultyTier, int64 GoldHeld);

	/**
	 * What taking the gem out of the item's socket `Socket` takes, against the Dismantling Dust held and whether the bag
	 * has room for the gem (a stack of it, a free cell, or the cell the last Dust frees).
	 *
	 * REFUSED for no item, no such socket, an empty socket, no Dust, no room, and too little gold.
	 */
	static FCataclysmForgeSocketing UnsocketGemOf(const FCataclysmItem& Item, int32 Socket, int32 DustHeld,
												  bool bRoomForTheGem, int32 DifficultyTier, int64 GoldHeld);

	/**
	 * What the first press of a socketing says, before the second does it: "Press again to socket Of The Viper (Superb)
	 * into the Attack socket: one Jeweler's Setting Agent, residue 87 to 92, 1 day of the run." or "Press again to
	 * unsocket Of The Viper (Superb): one Dismantling Dust, residue 87 to 90, 1 day of the run; the gem goes back to the
	 * bag."
	 */
	static FString SocketingQuestion(const FCataclysmForgeSocketing& Plan);

	//~ Adding a socket and changing a socket's type. Issue #46, slice G9, ruled by the coordinating session on 2026-11-22.
	//
	// THE CRAFTING SHEET'S OPERATIONS ROWS, `game/Data/ForgeOperations.csv`: "Add Socket | Shattered Core | 15 CR | 3",
	// Base Gold 750, and "Change Socket Type | Focusing Lens | 10 CR | 2", Base Gold 500, through `DaysFor` and `GoldFor`
	// as every craft. An added socket is empty and its type is ROLLED evenly among Attack, Defense and Utility, as a
	// drop rolls one; a changed socket takes the type the player CHOOSES, among the two it is not. Neither touches a gem:
	// a socket holding one is not changed ("Unsocket the gem first", from the design's "There is no free removal"), and
	// a socket for any gem is not offered a change, since a change could only narrow it.

	/** The row of the material Add Socket consumes, one each: `game/Data/CraftingMaterials.csv`. */
	static const TCHAR* ShatteredCoreRow;

	/** The row of the material Change Socket Type consumes, one each. */
	static const TCHAR* FocusingLensRow;

	/** The two operations' rows in `game/Data/ForgeOperations.csv`, whose Base Gold `BaseGoldOf` reads. */
	static const TCHAR* AddSocketOperationRow;
	static const TCHAR* ChangeSocketTypeOperationRow;

	/** The residue each adds, the sheet's CR impact. */
	static constexpr float AddSocketResidue = 15.0f;
	static constexpr float ChangeSocketTypeResidue = 10.0f;

	/** The days each costs before the residue's time penalty, the sheet's Base Days. */
	static constexpr int32 AddSocketBaseDays = 3;
	static constexpr int32 ChangeSocketTypeBaseDays = 2;

	/** The salt an added socket's roll mixes into the run's seed: "SOCK". */
	static constexpr int32 AddedSocketSalt = 0x534F434B;

	/**
	 * What adding a socket to the item takes, against the most sockets its slot takes and the Shattered Cores held.
	 *
	 * REFUSED for no item, a slot whose maximum is not known (below 0), an item at its maximum ("That piece holds its
	 * most sockets, 6."), no Shattered Core, and too little gold.
	 */
	static FCataclysmForgeSocketWork AddSocketOf(const FCataclysmItem& Item, int32 MaxSockets, int32 CoresHeld,
												 int32 DifficultyTier, int64 GoldHeld);

	/**
	 * What making the item's socket `Socket` a socket of `NewType` takes, against the Focusing Lenses held.
	 *
	 * REFUSED for no item, no such socket, a socket for any gem, a socket holding a gem ("Unsocket the gem first."), a
	 * new type of Any, the socket's own type, no Focusing Lens, and too little gold.
	 */
	static FCataclysmForgeSocketWork ChangeSocketTypeOf(const FCataclysmItem& Item, int32 Socket,
														ECataclysmSocketType NewType, int32 LensesHeld,
														int32 DifficultyTier, int64 GoldHeld);

	/**
	 * EVERYTHING THE ITEM SAVES, WRITTEN OUT AS TEXT, for the seed of an added socket's type: its base, gear level and
	 * damage types; each affix's row, tier, roll and damage types; its enchantment count and each enchantment's rows and
	 * rolls; each socket's type, gem and rarity; its residue, kills and whether it was made at creation. The rows'
	 * TEXT, not their FName hashes, which differ between game sessions. The same text is written in Python by
	 * `tools/tests/test_the_forge_adds_and_changes_sockets.py`. Ruled by the coordinating session on 2026-11-22,
	 * option (a): items carry no saved identifier, so their contents tell them apart.
	 */
	static FString ItemSeedText(const FCataclysmItem& Item);

	/**
	 * The seed of an added socket's type: `CataclysmSeedMix::Mix` of the run's `WalkSeedMask`, `AddedSocketSalt` and the
	 * run's day, mixed again with the CRC of `ItemSeedText` (`FCrc::StrCrc32`) and the item's socket count. The same
	 * item on the same day of the same run rolls the same type, so a reload cannot change it.
	 */
	static int32 AddedSocketSeed(const FCataclysmItem& Item, int32 WalkSeedMask, int32 Day);

	/** The type an added socket takes: `UCataclysmGems::RollSocketType` on a stream of `AddedSocketSeed`. */
	static ECataclysmSocketType AddedSocketType(const FCataclysmItem& Item, int32 WalkSeedMask, int32 Day);

	/**
	 * What the first press of one says, before the second does it: "Press again to add a socket: one Shattered Core,
	 * 750 gold, residue 40 to 55, 3 days of the run; its type is rolled." or "Press again to make socket 2 a Defense
	 * socket: one Focusing Lens, 500 gold, residue 40 to 50, 2 days of the run."
	 */
	static FString SocketWorkQuestion(const FCataclysmForgeSocketWork& Plan);

	//~ The three crafts that change one affix. Issue #47, slice F1, ruled by the coordinating session on 2026-11-22.
	//
	// THE CRAFTING SHEET'S OPERATIONS ROWS, `game/Data/ForgeOperations.csv`: "Reroll Affix Value | Corrupted Mote | 2 CR |
	// 0", Base Gold 250; "Perfect Roll Attempt | Primal Spark | 50 CR | 5", Base Gold 1,250; "Upgrade Affix Tier | Potency
	// Crystal | 10 CR*(Current affix tier) | 1(current affix tier)", Base Gold 250. Each through `DaysFor` and `GoldFor`
	// as every craft. Each acts on ONE AFFIX THE PLAYER CHOOSES (Q1, Q7). A reroll draws the roll again evenly anywhere
	// in the tier's range, so it can fall (Q2); a perfecting makes the roll the top, certainly, from the design's "Forces
	// the maximum stat roll" (Q6: the row's "Attempt" is its name, not a rule); an upgrade raises the tier by one to 7 and
	// KEEPS THE ROLL, the same place in the new tier's range (Q12), and its Base Gold is the sheet's flat 250, which
	// `GoldFor` multiplies by the residue that each step adds (Q13). An affix's value is worked out from its tier and
	// roll when it is read (`FCataclysmRolledAffix::Roll`), so no other field changes.

	/** The rows of the materials the three consume, one each: `game/Data/CraftingMaterials.csv`. */
	static const TCHAR* CorruptedMoteRow;
	static const TCHAR* PrimalSparkRow;
	static const TCHAR* PotencyCrystalRow;

	/** The three operations' rows in `game/Data/ForgeOperations.csv`, whose Base Gold `BaseGoldOf` reads. */
	static const TCHAR* RerollAffixValueOperationRow;
	static const TCHAR* PerfectRollOperationRow;
	static const TCHAR* UpgradeAffixTierOperationRow;

	/** The residue a reroll and a perfecting add, the sheet's CR impact. */
	static constexpr float RerollAffixValueResidue = 2.0f;
	static constexpr float PerfectRollResidue = 50.0f;

	/** The residue an upgrade adds for each tier the affix has before it: "10 CR*(Current affix tier)". */
	static constexpr float UpgradeAffixTierResiduePerTier = 10.0f;

	/** The days a reroll and a perfecting cost before the residue's time penalty. An upgrade's are the affix's tier. */
	static constexpr int32 RerollAffixValueBaseDays = 0;
	static constexpr int32 PerfectRollBaseDays = 5;

	/** The salt a rerolled affix's draw mixes into the run's seed: "ROLL". */
	static constexpr int32 RerolledAffixSalt = 0x524F4C4C;

	/** The row of the material a craft consumes. */
	static FName MaterialFor(ECataclysmForgeAffixCraft Craft);

	/**
	 * What one of the three crafts takes on the item's affix `Affix`, against the material of that craft held.
	 *
	 * REFUSED for no item, no such affix, a perfecting of a roll at the top already, an upgrade of an affix at tier 7, no
	 * material, and too little gold. A reroll of a roll at the top is not refused: it can fall, and the player chose it.
	 */
	static FCataclysmForgeAffixWork AffixWorkOf(const FCataclysmItem& Item, ECataclysmForgeAffixCraft Craft, int32 Affix,
												int32 MaterialHeld, int32 DifficultyTier, int64 GoldHeld);

	/**
	 * The seed of a rerolled affix's roll: `CataclysmSeedMix::Mix` of the run's `WalkSeedMask`, `RerolledAffixSalt` and the
	 * run's day, mixed again with the CRC of `ItemSeedText` and the affix's place, as an added socket's seed is. The same
	 * item on the same day of the same run draws the same roll, so a reload cannot change it; the reroll changes the item,
	 * so a second reroll draws another.
	 */
	static int32 RerolledAffixSeed(const FCataclysmItem& Item, int32 Affix, int32 WalkSeedMask, int32 Day);

	/** The roll a reroll draws: `FRandomStream::FRand` on a stream of `RerolledAffixSeed`, from 0 up to but not 1. */
	static float RerolledAffixRoll(const FCataclysmItem& Item, int32 Affix, int32 WalkSeedMask, int32 Day);

	/** A roll as the screen and the lines say it: "50 percent", its fraction of the tier's range, rounded. */
	static int32 RollPercent(float Roll);

	/**
	 * What the first press of one says, before the second does it: "Press again to reroll affix 1: one Corrupted Mote,
	 * 250 gold, residue 40 to 42, 0 days of the run; its roll, now 50 percent of tier 2's range, is drawn again and can
	 * fall." / "Press again to perfect affix 1, from 50 to 100 percent of tier 2's range: one Primal Spark, ..." / "Press
	 * again to raise affix 1 from tier 2 to tier 3, its roll kept at 50 percent: one Potency Crystal, ..."
	 */
	static FString AffixWorkQuestion(const FCataclysmForgeAffixWork& Plan);

	//~ Adding a chosen affix, and shifting an affix to another stat. Issue #47, slice F2, ruled by the coordinating
	// session on 2026-11-22.
	//
	// THE CRAFTING SHEET'S OPERATIONS ROWS, `game/Data/ForgeOperations.csv`: "Deterministic Affix | Schematic Fragments |
	// 5 CR per tier of affix | 1 day per tier of affix. (Max of 8)", Base Gold 250, "Guaranteed Affix."; and "Affix
	// Shifting (Stat Type) | Crystal of Instability | 15 CR | 1", Base Gold 250, "Rerolls stat type ... Can't be used on
	// enchantments.", through `DaysFor` and `GoldFor` as every craft.
	//
	// AN ADDITION takes the affix and the tier the player CHOOSES, T1 to T7 (Q8), among the affixes a drop could put on
	// the piece's slot, prefix or suffix (Q10), never one of a stat group the piece holds; its roll is drawn within the
	// tier (Q11). It needs a free slot of the four, so a piece with an enchantment, whose four are always full, takes
	// none; it promotes the piece one rarity, as the design's "Adding an affix promotes the piece" says. No limit on how
	// many a piece takes (Q9). A SHIFT draws a new affix from the same position's pool, never the affix it was and never
	// one of a group the piece holds (Q3), and KEEPS ITS TIER AND ROLL (Q4); a resistance's damage types are drawn again,
	// as a drop draws them (Q5). Both draws are seeded from the item, the run's mask and the day, as a reroll's is.

	/** The rows of the materials the two consume, one each: `game/Data/CraftingMaterials.csv`. */
	static const TCHAR* SchematicFragmentsRow;
	static const TCHAR* CrystalOfInstabilityRow;

	/** The two operations' rows in `game/Data/ForgeOperations.csv`, whose Base Gold `BaseGoldOf` reads. */
	static const TCHAR* DeterministicAffixOperationRow;
	static const TCHAR* AffixShiftingOperationRow;

	/** The residue an addition adds for each tier of the affix added: "5 CR per tier of affix". */
	static constexpr float DeterministicAffixResiduePerTier = 5.0f;

	/** An addition's base days are the affix's tier, "(Max of 8)". */
	static constexpr int32 DeterministicAffixMaxBaseDays = 8;

	/** The residue a shift adds and its base days, the sheet's CR impact and Base Days. */
	static constexpr float AffixShiftingResidue = 15.0f;
	static constexpr int32 AffixShiftingBaseDays = 1;

	/** The salts an addition's and a shift's draws mix into the run's seed: "SCHM" and "SHFT". */
	static constexpr int32 DeterministicAffixSalt = 0x5343484D;
	static constexpr int32 ShiftedAffixSalt = 0x53484654;

	/** The slot the item's base occupies, "Head" or "Weapon", as the Affixes table's `AllowedSlots` names it; empty when its base is not found. */
	static FString SlotOf(const FCataclysmItem& Item, const UDataTable* BaseTable);

	/** Every stat group the item's affixes occupy, `UCataclysmDropRoll::GroupsOf` of each with its damage types. */
	static void HeldGroupsOf(const FCataclysmItem& Item, const UDataTable* AffixTable, TSet<FString>& OutGroups);

	/**
	 * The affixes an addition may put on the item, in the table's order: every row a drop could roll on its slot, prefix
	 * or suffix, none of a stat group the item holds. A resistance family covering some damage types is offered while at
	 * least as many types as it covers are not held. Empty when its base is not found.
	 */
	static TArray<FName> AddableAffixesFor(const FCataclysmItem& Item, const UDataTable* AffixTable,
										   const UDataTable* BaseTable);

	/**
	 * The seed of an addition's or a shift's draw: `CataclysmSeedMix::Mix` of the run's `WalkSeedMask`, the craft's salt
	 * and the run's day, mixed again with the CRC of `ItemSeedText` and `Index`, as a reroll's seed is.
	 */
	static int32 AffixCraftSeed(const FCataclysmItem& Item, int32 Salt, int32 Index, int32 WalkSeedMask, int32 Day);

	/**
	 * What adding the affix `AffixRow` at `Tier` to the item takes, against the Schematic Fragments held. The roll, and a
	 * resistance family's damage types among those the item does not hold, are drawn from a stream of `Seed`.
	 *
	 * REFUSED for no item, a base not found, no free slot of the four, a tier outside 1 to 7, an affix the item cannot
	 * take (not on its slot, or of a stat group it holds), no Schematic Fragments, and too little gold.
	 */
	static FCataclysmForgeAffixChange DeterministicAffixOf(const FCataclysmItem& Item, FName AffixRow, int32 Tier,
														   int32 Seed, const UDataTable* AffixTable,
														   const UDataTable* BaseTable, int32 FragmentsHeld,
														   int32 DifficultyTier, int64 GoldHeld);

	/**
	 * What shifting the item's affix `Affix` to another stat takes, against the Crystals of Instability held. The new
	 * affix, and its damage types, are drawn from a stream of `Seed` among `UCataclysmDropRoll::CandidatesFor` of the
	 * slot and the affix's position, less the affix's own row and any candidate of a stat group the item holds.
	 *
	 * REFUSED for no item, a base not found, no such affix, no other affix able to take its place, no Crystal of
	 * Instability, and too little gold.
	 */
	static FCataclysmForgeAffixChange AffixShiftOf(const FCataclysmItem& Item, int32 Affix, int32 Seed,
												   const UDataTable* AffixTable, const UDataTable* BaseTable,
												   int32 CrystalsHeld, int32 DifficultyTier, int64 GoldHeld);

	/** An affix row's name as the sheet gives it, "Flat armor"; the row key when the table does not hold it. */
	static FString AffixNameOf(FName AffixRow, const UDataTable* AffixTable);

	/**
	 * What the first press of one says, before the second does it: "Press again to add Flat armor at tier 3 as affix 2,
	 * making the piece Quality: Schematic Fragments, 450 gold, residue 40 to 55, 3 days of the run; its roll is drawn in
	 * the tier." or "Press again to shift affix 1, Flat evasion, to another stat, keeping tier 2 and its roll: one Crystal
	 * of Instability, 450 gold, residue 40 to 55, 1 day of the run; the new stat is drawn."
	 */
	static FString AffixChangeQuestion(const FCataclysmForgeAffixChange& Plan, const UDataTable* AffixTable);

	//~ The three crafts that change a weapon's damage types. Issue #47, slice F4, ruled by the coordinating session on
	// 2026-11-22.
	//
	// THE CRAFTING SHEET'S OPERATIONS ROWS, `game/Data/ForgeOperations.csv`: "Infuse Random Damage Type | Elemental
	// Cinder | 10 CR | 1", Base Gold 250; "Infuse Specific Damage Type | Sovereign Ingot | 50 CR | 5", Base Gold 1,250;
	// "Change Damage Type | Prismatic Catalyst | 20 CR | 0", Base Gold 250; through `DaysFor` and `GoldFor` as every
	// craft. WEAPONS ONLY (Q19): a weapon's damage types are its `DamageTypes`, drawn by the drop among the types
	// designed for its weapon type (`UCataclysmDropRoll::DamageTypesAvailableTo`).
	//
	// THE MAXIMUM IS THE DROP'S, `UCataclysmDropRoll::MaxDamageTypesFor`, and the two infusions are refused at it (Q20).
	// THE TYPES AN ENCHANTMENT ADDS ARE ALREADY IN THE WEAPON'S LIST: "This weapon has 2-4 damage types"
	// (`DamageTypesCarriedBy`) is a count the drop tops the list up to (`TopUpDamageTypes`), under the same maximum, and
	// never a ceiling. So counting the list counts them, as the drop does (Q21, resolved 2026-11-22). A change works at
	// the maximum, and its new type is drawn among those the weapon lacks (Q22).
	//
	// THE WEAPON MADE AT CREATION IS REFUSED (ruled 2026-11-22, option (a), issue #2845): its damage types are set back
	// to the one chosen for the character on every load, by `UCataclysmEquipmentComponent::CarryCreationDamageType`, so a
	// craft on it would be undone.

	/** The rows of the materials the three consume, one each: `game/Data/CraftingMaterials.csv`. */
	static const TCHAR* ElementalCinderRow;
	static const TCHAR* SovereignIngotRow;
	static const TCHAR* PrismaticCatalystRow;

	/** The three operations' rows in `game/Data/ForgeOperations.csv`, whose Base Gold `BaseGoldOf` reads. */
	static const TCHAR* InfuseRandomDamageTypeOperationRow;
	static const TCHAR* InfuseSpecificDamageTypeOperationRow;
	static const TCHAR* ChangeDamageTypeOperationRow;

	/** The residue each adds, the sheet's CR impact. */
	static constexpr float InfuseRandomDamageTypeResidue = 10.0f;
	static constexpr float InfuseSpecificDamageTypeResidue = 50.0f;
	static constexpr float ChangeDamageTypeResidue = 20.0f;

	/** The days each costs before the residue's time penalty, the sheet's Base Days. */
	static constexpr int32 InfuseRandomDamageTypeBaseDays = 1;
	static constexpr int32 InfuseSpecificDamageTypeBaseDays = 5;
	static constexpr int32 ChangeDamageTypeBaseDays = 0;

	/** The salts a random infusion's and a change's draws mix into the run's seed: "INFS" and "CHNG". */
	static constexpr int32 InfusedDamageTypeSalt = 0x494E4653;
	static constexpr int32 ChangedDamageTypeSalt = 0x43484E47;

	/** The refusal for the weapon made at creation, issue #2845. */
	static const TCHAR* CreationWeaponRefusal;

	/** The row of the material a craft consumes. */
	static FName MaterialFor(ECataclysmForgeDamageTypeCraft Craft);

	/** Whether the item's base is a weapon that carries damage types: a `MaxDamageTypes` above 0 and a weapon type. */
	static bool CarriesDamageTypes(const FCataclysmItem& Item, const UDataTable* BaseTable);

	/**
	 * The damage types designed for the weapon's type that it does not carry, in `DamageTypeNames` order; empty for an
	 * item that is no weapon.
	 */
	static TArray<FName> DamageTypesOpenTo(const FCataclysmItem& Item, const UDataTable* WeaponSkillTable,
										   const UDataTable* BaseTable);

	/** The most damage types the weapon may carry, `UCataclysmDropRoll::MaxDamageTypesFor`; 0 for no weapon. */
	static int32 MaxDamageTypesOf(const FCataclysmItem& Item, const UDataTable* WeaponSkillTable,
								  const UDataTable* BaseTable);

	/**
	 * What one of the three crafts takes on the item. `Chosen` is the type a specific infusion adds, or the type a change
	 * replaces; a random infusion ignores it. A random infusion's and a change's new type are drawn from a stream of
	 * `Seed` among `DamageTypesOpenTo`.
	 *
	 * REFUSED for no item, a base not found, an item that is no weapon, the weapon made at creation, an infusion at the
	 * maximum, no type left to draw, a chosen type the weapon carries already or cannot carry, a change of a type it
	 * does not carry, no material, and too little gold.
	 */
	static FCataclysmForgeDamageTypeWork DamageTypeWorkOf(const FCataclysmItem& Item,
														  ECataclysmForgeDamageTypeCraft Craft, FName Chosen,
														  int32 Seed, const UDataTable* WeaponSkillTable,
														  const UDataTable* BaseTable, int32 MaterialHeld,
														  int32 DifficultyTier, int64 GoldHeld);

	/**
	 * What the first press of one says, before the second does it: "Press again to infuse a random damage type: one
	 * Elemental Cinder, 450 gold, residue 40 to 50, 1 day of the run; the type is drawn among those the weapon can
	 * take." / "Press again to infuse Death: one Sovereign Ingot, ..." / "Press again to change War to another damage
	 * type: one Prismatic Catalyst, ...; the new type is drawn among those the weapon lacks."
	 */
	static FString DamageTypeQuestion(const FCataclysmForgeDamageTypeWork& Plan);
};
