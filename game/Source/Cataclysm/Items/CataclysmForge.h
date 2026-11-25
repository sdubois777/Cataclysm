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

/**
 * The two residue tools a player may add to an item craft's own press: a Chaos Stabilizer, and some Tainted Shards.
 * Issue #47, slice F5 (Q23): an option spent in the craft it is pressed with, never a saved "next craft".
 */
struct FCataclysmForgeCraftOptions
{
	/** Use a Chaos Stabilizer: the residue the craft adds is halved (Q27). */
	bool bChaosStabilizer = false;

	/** Tainted Shards to use: each removes one day of the residue's time penalty, down to the craft's base days (Q24). */
	int32 TaintedShards = 0;
};

/** One material an item craft takes, and how many. Its tier is read from the crafting material table. #47, F5. */
struct FCataclysmForgeMaterialTake
{
	FName Material;
	int32 Count = 1;
};

/**
 * What the options do to one item craft: its residue and days after them, the gold they cost on top of the craft's,
 * the materials they take, and why they cannot be used. Issue #47, slice F5.
 */
struct FCataclysmForgeOptionEffect
{
	bool bPossible = true;
	FString Refusal;

	/** The craft's residue after it and its days, with the options; equal to the plan's without them. */
	float ResidueAfter = 0.0f;
	int32 Days = 0;

	/** What the options saved: the residue not added, and the days not spent. */
	float ResidueSaved = 0.0f;
	int32 DaysSaved = 0;

	/** Whether a Chaos Stabilizer is used, and how many Tainted Shards. */
	bool bStabilizerUsed = false;
	int32 ShardsUsed = 0;

	/** The residue after and the days with the options alone, before the empire tree's crafting nodes (#2741's
	 *  crafting group, slices C1 and C2): what the options' sentences say. Equal to `ResidueAfter` and `Days` when no
	 *  node applies. */
	float OptionsResidueAfter = 0.0f;
	int32 OptionsDays = 0;

	/** Master's Touch's draw landed: the residue after the craft is 0. */
	bool bResidueReset = false;

	/** The gold the options cost, on top of the craft's own (G1), and the materials they take. */
	int64 Gold = 0;
	TArray<FCataclysmForgeMaterialTake> Materials;
};

/** What halving an item's residue with a Purified Essence would take, or why it cannot. Issue #47, slice F5. */
struct FCataclysmForgeCleanse
{
	bool bPossible = false;
	FString Refusal;

	/** The material it consumes, one of it, and how many are held. */
	FName Material;
	int32 MaterialHeld = 0;

	/** The item's residue before, and half of it after. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: one for each 500 residue or part of it, at least one (Q25). */
	int32 Days = 0;

	/** The days before the empire tree's day nodes, and whether Master's Touch left the residue at 0 (slices C1, C2). */
	int32 DaysBeforeTheTree = 0;
	bool bResidueReset = false;

	/** The gold it costs at the difficulty tier of the run, and the gold held. */
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
 * What imprinting an enchantment on an item, or changing one enchantment's negative, would take, or why it cannot.
 * Issue #47, slice F3.
 */
struct FCataclysmForgeEnchantmentWork
{
	bool bPossible = false;
	FString Refusal;

	/** True to imprint an enchantment, false to change a negative. */
	bool bImprint = false;

	/** An imprint's affix given up, from 0, and its row. */
	int32 Affix = INDEX_NONE;
	FName AffixRow;

	/** A change's enchantment pair, from 0, as it stands before the change. */
	int32 Pair = INDEX_NONE;
	FCataclysmRolledEnchantment Before;

	/**
	 * The pair the craft drew: an imprint's new pair or a change's pair with its new negative. EMPTY IN A PLAN, filled
	 * only once the craft is done, since it is drawn then, as an added socket's type is.
	 */
	FCataclysmRolledEnchantment Drawn;

	/** An imprint's rarity before and after, and the gear level the rarity after needs (`GearLevelGate`). */
	ECataclysmRarity RarityBefore = ECataclysmRarity::Everyday;
	ECataclysmRarity RarityAfter = ECataclysmRarity::Everyday;
	int32 GearLevelNeeded = 0;

	/** The material it consumes, one of it, and how many are held. */
	FName Material;
	int32 MaterialHeld = 0;

	/** The item's residue before the craft, and after it adds its own. */
	float ResidueBefore = 0.0f;
	float ResidueAfter = 0.0f;

	/** The days of the run it costs: its base days, and the residue's time penalty read before the craft. */
	int32 Days = 0;

	/** The gold it costs at the difficulty tier of the run, and the gold held. */
	int64 Gold = 0;
	int64 GoldHeld = 0;
};

/**
 * What dismantling a carried piece gives, or why it cannot. Issue #2848, slice S2, ruled by the coordinating session on
 * 2026-11-22 (the plan's sections 6 to 8, and D1 to D5).
 *
 * TWO KINDS OF YIELD. What is CERTAIN is known before the draw: the socketed gems, back to the bag, and the refund, one
 * upgrade stone of each level the piece was raised through. What is DRAWN is each fractional yield, its whole part
 * always given and its fraction as one more with that chance (D5): Schematic Fragments, Dismantling Dust and the base
 * upgrade stones. A plan holds the chances; `UCataclysmForge::DrawDismantle` fills what was drawn.
 */
struct FCataclysmForgeDismantle
{
	bool bPossible = false;
	FString Refusal;

	/** The piece's rarity, which its fragment and stone yields and Disassembly Expert read. */
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;

	/** The two empire tree nodes' increase on the drawn yields: Salvage Protocol's and Disassembly Expert's, added. */
	float Increase = 0.0f;

	/** Each drawn yield's expected count, the increase included: what the question shows. */
	float FragmentsExpected = 0.0f;
	float DustExpected = 0.0f;
	float StonesExpected = 0.0f;

	/** The refund, certain: one stone of each level from `RefundFromLevel` + 1 to the piece's gear level. */
	int32 RefundFromLevel = 0;
	TArray<FName> RefundStones;

	/** The socketed gems, certain, each with its rarity. */
	TArray<TPair<FName, ECataclysmRarity>> Gems;

	/** True once drawn; then the drawn counts and each base stone's row. */
	bool bDrawn = false;
	int32 Fragments = 0;
	int32 Dust = 0;
	TArray<FName> BaseStones;
};

/**
 * THE FORGE'S GOLD AFTER THE EMPIRE TREE'S FIVE FORGE GOLD NODES, for one craft (#47 slice F6a): what a plan's price is
 * multiplied by, for each crafting material tier. `UCataclysmForgeCraft::PricingFor` works it out once from the tree,
 * each tier's factor by `UCataclysmEmpireTree::CraftGoldFactorFor`, and passes it to the plan, which applies it to its
 * price before its gold refusal, so a player who can afford the discounted price is not refused.
 *
 * A TRAILING PARAMETER WITH A DEFAULT OF NO DISCOUNT, the coordinating session's ruling A of 2026-11-24: every factor is
 * 1, so the plan tests that pass no pricing price at full, and a Python check makes sure every plan call the game makes
 * passes the character's. Forge Upgrades' tenth is rolled once for the craft and is in every factor, so the craft's
 * options take the same roll at their own materials' tiers (ruling B of 2026-11-24).
 */
struct FCataclysmForgePricing
{
	/** The highest crafting material tier (`MaterialTiers.csv`: 1 Common to 5 Extremely Rare). */
	static constexpr int32 MaxMaterialTier = 5;

	/** The multiplier on a price, by its material's tier; tier 0 is a craft with no material (a gem combine). */
	float Factors[MaxMaterialTier + 1] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

	/** Whether Forge Upgrades' tenth landed for this craft. Every factor holds it already; this is kept to say so. */
	bool bTenthOff = false;

	/** The crafting material table, for a material's tier. None prices every material at tier 0's factor. */
	const UDataTable* MaterialTable = nullptr;

	/** The tier of `Material` in `MaterialTable`: 0 for none, or for a name the table does not hold. */
	int32 TierOf(FName Material) const;

	/** The factor for `Material`'s tier. */
	float FactorFor(FName Material) const;

	/** `Gold` times `FactorFor(Material)`, rounded, and at least 1 when `Gold` is: a discounted price is never free. */
	int64 Apply(int64 Gold, FName Material) const;

	/** The whole percent `FactorFor(Material)` takes off, rounded, for the screen. 0 with no discount. */
	int32 PercentOffFor(FName Material) const;
};

/**
 * What the empire tree's three forge material nodes keep of one item craft's crafting materials. #47 slice F6b, ruled by
 * the coordinating session on 2026-11-22 (Q11 to Q17) and 2026-11-25 (N1 to N5):
 *   - Material Conservation's draw keeps the tier 1 and 2 materials, Resource Management's the tier 3 to 5 ones ("Rare"
 *     is tier 3 and above, Q14), and Tempting Fate's, on an item whose residue before the craft is above
 *     `UCataclysmForge::TemptingFateResidueAbove` (Q15, N5), every material;
 *   - each drawn once for the craft from the item and the day (Q12, Q13), independently (Q16), so a material is kept
 *     when any node covering it lands; a node keeps all of its materials or none (Q12);
 *   - the option materials count by their tier (Q17); a gem is not a crafting material (N2), and the combines never
 *     reach it (Q11).
 * Drawn by `UCataclysmForge::KeepingOn`; the craft takes every material as before and puts the kept ones back once it
 * can no longer fail, before its days pass (N1, `UCataclysmForgeCraft::DoItemCraft`).
 */
struct FCataclysmForgeKeeping
{
	/** Material Conservation's draw landed: the craft's tier 1 and 2 materials are kept. */
	bool bLowTierKept = false;

	/** Resource Management's draw landed: the craft's tier 3 to 5 materials are kept. */
	bool bRareKept = false;

	/** Tempting Fate's draw landed, on an item above the residue: every material the craft used is returned. */
	bool bAllReturned = false;

	/** The crafting material table, for a material's tier and name. None keeps nothing but what Tempting Fate returns. */
	const UDataTable* MaterialTable = nullptr;

	/** The node that keeps `Material` in this craft: "Tempting Fate" when its draw landed, else the tier's node when its
	 *  draw landed; empty when none keeps it. */
	FString NodeFor(FName Material) const;

	/** The takes of `Materials` that are kept, each whole (Q12), in their order. */
	TArray<FCataclysmForgeMaterialTake> KeptOf(const TArray<FCataclysmForgeMaterialTake>& Materials) const;

	/** "2 Upgrade Stone +3 (Material Conservation), 1 Chaos Stabilizer (Resource Management)" for the kept takes of
	 *  `Materials`, each with its node; empty when none is kept. */
	FString KeptPartsOf(const TArray<FCataclysmForgeMaterialTake>& Materials) const;
};

/**
 * What the empire tree's crafting nodes do to one forge item craft's residue and days. #2741's crafting group, slices C1
 * and C2, ruled by the coordinating session on 2026-11-25:
 *   - the residue the craft adds is multiplied by `ResidueFactor` (Tool Maintenance's, Limited Knowledge's on an item
 *     below +6, Heat Shielding's on a tier 3+ craft material, each compounded per point), and a Chaos Stabilizer keeps
 *     `StabilizedResidueShare` less `StabilizerShareReduced` of it (Stable Compounds); when Master's Touch's draw
 *     landed, the residue after the craft is 0;
 *   - the days, after the Tainted Shards: times `DaysFactor` (Industrial Speed, Sovereign's Assembly), less `DaysOff`
 *     (Master Craftsman), rounded; a craft of at least one day keeps one, a craft of none stays at none
 *     (`UCataclysmForge::TreeDaysFor`).
 * Worked out once by `UCataclysmForgeCraft::CostsFor` and passed to `OptionsOn` and `CleanseOf`, so the question and the
 * press read the same figures (G2). The default changes nothing.
 */
struct FCataclysmForgeCosts
{
	float ResidueFactor = 1.0f;
	float StabilizerShareReduced = 0.0f;
	bool bResidueReset = false;
	float DaysFactor = 1.0f;
	float DaysOff = 0.0f;
};

/**
 * What the empire tree's roll nodes do to an affix roll the forge draws. #2741's crafting group, slice C3, ruled by the
 * coordinating session on 2026-11-25: a reroll's roll (`UCataclysmForge::RerolledAffixRoll`), a Deterministic Affix's
 * (`DeterministicAffixOf`) and Rough Draft's affix's are drawn from `Floor` up to 1 (Master's Workshop, 0.02 a point),
 * and from `UCataclysmForge::TopQuarterFloor` up when Precision Crafting's own draw lands (3% a point), the higher of the
 * two. A shift and a tier upgrade keep the roll (F1 Q12), so neither reaches it. The default changes nothing.
 */
struct FCataclysmForgeRolls
{
	/** Master's Workshop: a roll is drawn from this up to 1. */
	float Floor = 0.0f;

	/** Precision Crafting: the chance a roll is drawn from `UCataclysmForge::TopQuarterFloor` up. */
	float TopQuarterChance = 0.0f;

	/** Whether Precision Crafting's draw lands for a roll drawn on `Seed`: its chance above nought and a draw on `Seed`
	 *  mixed with `UCataclysmForge::PrecisionCraftingSalt` below it, so the roll's own draws are untouched. */
	bool TopQuarterLands(int32 Seed) const;

	/** The roll for `Draw` (0 up to but not 1) drawn on `Seed`: `Low + (1 - Low) x Draw`, `Low` the floor, or 0.75 when
	 *  Precision Crafting's draw lands and that is higher. With neither node, `Draw` itself. */
	float Shape(float Draw, int32 Seed) const;
};

/**
 * The empire tree's three affix nodes that change the item of a forge item craft. #2741's crafting group, slices C3 and
 * C4, ruled by the coordinating session on 2026-11-25 (D1 to D3): once a craft has changed the item, on draws of their
 * own from the item as it stood and the day, in this order, each choosing among the affixes as they stand at its turn:
 *   - Rough Draft adds one affix (5% a point), when a slot of the four is free and its side has room, drawn as a shift
 *     draws one, at the drop's best tier for the run (`UCataclysmDropRoll::MaxAffixTierOnADrop`), its roll drawn with
 *     `Rolls`; the piece is promoted by its new affix count, as rarity is;
 *   - Apprentice Smith raises one regular affix below tier 7 a tier, its roll kept (1% a point);
 *   - Quality Materials makes one regular affix's roll below 1 its best, 1 (10% a point).
 * Worked out by `UCataclysmForgeCraft::AffixNodesFor`; applied by `UCataclysmForge::AffixNodesOn` in
 * `UCataclysmForgeCraft::DoItemCraft`, never on a dismantle. The default changes nothing.
 */
struct FCataclysmForgeAffixNodes
{
	float AddedChance = 0.0f;
	float TierRaisedChance = 0.0f;
	float RollMaxedChance = 0.0f;
	FCataclysmForgeRolls Rolls;
};

/** What the three affix nodes did to one craft's item; `INDEX_NONE` is a node that did nothing. Slices C3 and C4. */
struct FCataclysmForgeAffixNodesDone
{
	/** Rough Draft's affix: its place on the piece, its row and its tier. */
	int32 Added = INDEX_NONE;
	FName AddedRow;
	int32 AddedTier = 0;

	/** Apprentice Smith's affix: its place, its row and the tier it rose from. */
	int32 Raised = INDEX_NONE;
	FName RaisedRow;
	int32 RaisedFromTier = 0;

	/** Quality Materials' affix: its place, its row and the roll it had. */
	int32 Maxed = INDEX_NONE;
	FName MaxedRow;
	float MaxedFromRoll = 0.0f;

	bool Any() const { return Added != INDEX_NONE || Raised != INDEX_NONE || Maxed != INDEX_NONE; }
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
											int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

	/**
	 * What combining two stones of this level into one of the next takes, against the stones of this level held.
	 *
	 * REFUSED for a level outside 1 to 9 -- +10 is the highest stone -- for fewer than two held, and for too little
	 * gold (`CombineGoldFor` at rung `Level - 1`).
	 */
	static FCataclysmForgeCombine CombineOf(int32 Level, int32 StonesOfThisLevelHeld, int32 DifficultyTier,
											int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

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
												  int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

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
												int32 AgentsHeld, int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

	/**
	 * What taking the gem out of the item's socket `Socket` takes, against the Dismantling Dust held and whether the bag
	 * has room for the gem (a stack of it, a free cell, or the cell the last Dust frees).
	 *
	 * REFUSED for no item, no such socket, an empty socket, no Dust, no room, and too little gold.
	 */
	static FCataclysmForgeSocketing UnsocketGemOf(const FCataclysmItem& Item, int32 Socket, int32 DustHeld,
												  bool bRoomForTheGem, int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

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
												 int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

	/**
	 * What making the item's socket `Socket` a socket of `NewType` takes, against the Focusing Lenses held.
	 *
	 * REFUSED for no item, no such socket, a socket for any gem, a socket holding a gem ("Unsocket the gem first."), a
	 * new type of Any, the socket's own type, no Focusing Lens, and too little gold.
	 */
	static FCataclysmForgeSocketWork ChangeSocketTypeOf(const FCataclysmItem& Item, int32 Socket,
														ECataclysmSocketType NewType, int32 LensesHeld,
														int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

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
												int32 MaterialHeld, int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

	/**
	 * The seed of a rerolled affix's roll: `CataclysmSeedMix::Mix` of the run's `WalkSeedMask`, `RerolledAffixSalt` and the
	 * run's day, mixed again with the CRC of `ItemSeedText` and the affix's place, as an added socket's seed is. The same
	 * item on the same day of the same run draws the same roll, so a reload cannot change it; the reroll changes the item,
	 * so a second reroll draws another.
	 */
	static int32 RerolledAffixSeed(const FCataclysmItem& Item, int32 Affix, int32 WalkSeedMask, int32 Day);

	/** The roll a reroll draws: `FRandomStream::FRand` on a stream of `RerolledAffixSeed`, from 0 up to but not 1, placed
	 *  by the empire tree's roll nodes (`FCataclysmForgeRolls::Shape`, #2741's crafting slice C3). */
	static float RerolledAffixRoll(const FCataclysmItem& Item, int32 Affix, int32 WalkSeedMask, int32 Day,
								   const FCataclysmForgeRolls& Rolls = FCataclysmForgeRolls());

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
														   int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing(),
		const FCataclysmForgeRolls& Rolls = FCataclysmForgeRolls());

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
												   int32 CrystalsHeld, int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

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
														  int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

	/**
	 * What the first press of one says, before the second does it: "Press again to infuse a random damage type: one
	 * Elemental Cinder, 450 gold, residue 40 to 50, 1 day of the run; the type is drawn among those the weapon can
	 * take." / "Press again to infuse Death: one Sovereign Ingot, ..." / "Press again to change War to another damage
	 * type: one Prismatic Catalyst, ...; the new type is drawn among those the weapon lacks."
	 */
	static FString DamageTypeQuestion(const FCataclysmForgeDamageTypeWork& Plan);

	//~ Imprinting an enchantment and changing an enchantment's negative. Issue #47, slice F3, ruled by the coordinating
	//~ session on 2026-11-22 (the forge survey's Q14 to Q18).
	//
	// THE CRAFTING SHEET'S OPERATIONS ROWS, `game/Data/ForgeOperations.csv`: "Imprint Enchantment | Aetherial Shard |
	// 5 CR | 2", Base Gold 500, and "Change Enchantment Negative | Regret Essence | 100 CR | 7", Base Gold 1,750, through
	// `DaysFor` and `GoldFor` as every craft.
	//
	// AN IMPRINT ADDS ONE ENCHANTMENT AND GIVES UP ONE AFFIX: "Applying an enchantment to a Masterful piece makes it
	// Legendary, and the piece gives up a regular affix to do it" (`docs/Cataclysm_GDD_v2.md`, section VI). The pair is
	// RANDOM, a positive and its negative drawn as a drop draws them (Q14); the PLAYER CHOOSES the affix given up (Q15);
	// and only a piece with every slot filled, Masterful and up, is imprinted (Q16). The rarity it becomes must be one
	// its gear level reaches: the design's rarity table, "Legendary ... Requires gear level 4+", `GearLevelGate` in
	// `game/Data/GearRarity.csv`.
	//
	// A CHANGE DRAWS A DIFFERENT NEGATIVE AT RANDOM under the weight rule (Q17), never the same one again (Q18), and
	// keeps the positive and its roll. A set's pair is not changed: its negative is the set's own.

	/** The rows of the materials each consumes, one each: `game/Data/CraftingMaterials.csv`. */
	static const TCHAR* AetherialShardRow;
	static const TCHAR* RegretEssenceRow;

	/** The two operations' rows in `game/Data/ForgeOperations.csv`, whose Base Gold `BaseGoldOf` reads. */
	static const TCHAR* ImprintOperationRow;
	static const TCHAR* ChangeNegativeOperationRow;

	/** The residue each adds, the sheet's CR impact. */
	static constexpr float ImprintResidue = 5.0f;
	static constexpr float ChangeNegativeResidue = 100.0f;

	/** The days each costs before the residue's time penalty, the sheet's Base Days. */
	static constexpr int32 ImprintBaseDays = 2;
	static constexpr int32 ChangeNegativeBaseDays = 7;

	/** The salts each draw mixes into the run's seed: "IMPR" and "NEGA". */
	static constexpr int32 ImprintSalt = 0x494D5052;
	static constexpr int32 ChangeNegativeSalt = 0x4E454741;

	/**
	 * What imprinting an enchantment on the item, giving up its affix `Affix`, takes, against the Aetherial Shards held.
	 *
	 * REFUSED for no item, a piece with a slot left empty (below Masterful), a piece with no affix left (Cataclysmic), no
	 * such affix, a gear rarity table that cannot be read, a piece below the gear level the rarity after needs, no
	 * Aetherial Shard, and too little gold.
	 */
	static FCataclysmForgeEnchantmentWork ImprintOf(const FCataclysmItem& Item, int32 Affix, int32 ShardsHeld,
													const UDataTable* GearRarityTable, int32 DifficultyTier,
													int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

	/**
	 * What changing the negative of the item's enchantment pair `Pair` takes, against the Regret Essences held.
	 *
	 * REFUSED for no item, no such pair, a set's pair, and, read from `PositiveTable`, a positive not in it; no Regret
	 * Essence, and too little gold.
	 */
	static FCataclysmForgeEnchantmentWork ChangeNegativeOf(const FCataclysmItem& Item, int32 Pair, int32 EssencesHeld,
														   const UDataTable* PositiveTable, int32 DifficultyTier,
														   int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());

	/**
	 * The item an imprint makes: the affix `Affix` given up, `Pair` added and the enchantment count raised with it (the
	 * two "MUST AGREE", `FCataclysmItem`), and, when the new pair says the weapon carries more damage types ("This weapon
	 * has 2-4 damage types"), the types topped up from `Stream` as a drop tops them up. Separate from the draw so a test
	 * can apply a pair it names.
	 *
	 * @return false for no such affix or a piece the imprint would leave with no rarity
	 */
	static bool ApplyImprint(const FCataclysmItem& Item, int32 Affix, const FCataclysmRolledEnchantment& Pair,
							 const UDataTable* BaseTable, const UDataTable* WeaponSkillTable,
							 const UDataTable* EnchantmentEffectTable, FRandomStream& Stream, FCataclysmItem& OutItem);

	/**
	 * The pair an imprint on the item draws: `UCataclysmDropRoll::RollEnchantments` for one pair on the item's slot
	 * (`SlotOf`), the pairs it holds counting as taken, on a stream of slice F2's `AffixCraftSeed` with `ImprintSalt` and
	 * the item's pair count as its index. READ FROM THE ITEM BEFORE THE CRAFT, so the same pair is drawn whichever affix
	 * the player gives up, and the same item on the same day of the same run draws the same, so a reload cannot change
	 * it. `OutStream` is that stream after the draw, which `ApplyImprint` tops up damage types from.
	 */
	static bool DrawImprint(const FCataclysmItem& Item, const UDataTable* BaseTable, const UDataTable* PositiveTable,
							const UDataTable* NegativeTable, int32 WalkSeedMask, int32 Day,
							FCataclysmRolledEnchantment& OutPair, FRandomStream& OutStream);

	/**
	 * The pair a change of the item's pair `Pair` draws: `UCataclysmDropRoll::DrawReplacementNegative` on the item's slot
	 * (`SlotOf`), on a stream of slice F2's `AffixCraftSeed` with `ChangeNegativeSalt` and `Pair` as its index, read from
	 * the item before the craft, as an imprint's is.
	 */
	static bool DrawChangedNegative(const FCataclysmItem& Item, int32 Pair, const UDataTable* BaseTable,
									const UDataTable* PositiveTable, const UDataTable* NegativeTable,
									int32 WalkSeedMask, int32 Day, FCataclysmRolledEnchantment& OutPair);

	/**
	 * What the first press of one says, before the second does it, `Named` being the affix given up or the enchantment
	 * changed as the item's tool tip reads it: "Press again to imprint an enchantment, giving up +12 to strength: one
	 * Aetherial Shard, 500 gold, residue 150 to 155, 2 days of the run; the piece becomes Legendary and the enchantment
	 * is drawn at random." or "Press again to change the drawback of <enchantment>: one Regret Essence, 1,750 gold,
	 * residue 200 to 300, 9 days of the run; a different drawback is drawn at random."
	 */
	static FString EnchantmentWorkQuestion(const FCataclysmForgeEnchantmentWork& Plan, const FString& Named);

	//~ The two crafting options of the empire tree's centre milestones. Issue #47, slice F6, ruled by the coordinating
	//~ session on 2026-11-22 (R1 to R8).
	//
	// THE TEXTS, `docs/Empire_Skill_Tree_Keystones.md`: The Soul Forge, "When improving the level of an item, you have a
	// 15% chance to increase its level by 2 instead of 1, up to the maximum of 10"; The Master Tinkerer, "When enchanting,
	// you have a 5% chance to hit a critical success, doubling the rolls of the enchantment and completing the craft
	// instantly". The chances are `FCataclysmEmpireTreePlayerBonuses::UpgradeExtraLevelChance` and `EnchantCriticalChance`.
	//
	// EACH DRAWS ON A STREAM OF ITS OWN SALT (R3), so a craft without the option draws nothing more and every draw a craft
	// made before this slice is made the same. The same item on the same day of the same run draws the same, so a reload
	// cannot change the outcome, which is not shown before the press (R8).

	/** The salts of the two draws: "SOUL" and "CRIT". */
	static constexpr int32 SoulForgeSalt = 0x534F554C;
	static constexpr int32 EnchantCriticalSalt = 0x43524954;

	// ---- #47 slice F6a: Forge Upgrades' tenth, "1% chance per point to reduce the cost of a craft in the forge by 10%".
	// ROLLED IN THE PLAN, NOT AT THE PRESS (Q10, as `AddedSocketSeed`): the price on the screen is the price paid, a reload
	// cannot roll it again, and the screen can say it landed.

	/** The salt of Forge Upgrades' draw: "FGLD". */
	static constexpr int32 ForgeUpgradesSalt = 0x46474C44;

	/** The seed of Forge Upgrades' draw for a craft on `Item`: the item and the day of the run (Q10). */
	static int32 ForgeUpgradesSeed(const FCataclysmItem& Item, int32 WalkSeedMask, int32 Day);

	/** The seed of Forge Upgrades' draw for a combine, which has no item: the rung and the day (Q10). */
	static int32 CombineForgeUpgradesSeed(int32 Rung, int32 WalkSeedMask, int32 Day);

	/** Whether Forge Upgrades' tenth lands: `Chance` above nought and a draw on `Seed` below it. */
	static bool ForgeUpgradesTenthOff(float Chance, int32 Seed);

	// ---- #47 slice F6b: the three material nodes (`FCataclysmForgeKeeping`). Drawn as Forge Upgrades is, from the item
	// and the day (Q13), each on a salt of its own (Q16), so the question can say what will be kept and a reload cannot
	// draw again.

	/** The salts of the three draws: "FMCN" (Material Conservation), "FRMG" (Resource Management), "FTMF" (Tempting
	 *  Fate). */
	static constexpr int32 MaterialConservationSalt = 0x464D434E;
	static constexpr int32 ResourceManagementSalt = 0x46524D47;
	static constexpr int32 TemptingFateSalt = 0x46544D46;

	/** Tempting Fate's "While your item's CR is above 500": an item's residue before the craft must be MORE than this (N5). */
	static constexpr float TemptingFateResidueAbove = 500.0f;

	/** The highest tier Material Conservation keeps ("T2 and below"); Resource Management keeps every tier above it (Q14). */
	static constexpr int32 MaterialConservationHighestTier = 2;

	/**
	 * The three draws for an item craft on `Item` on `Day`: each chance above nought and a draw on its own seed
	 * (`AffixCraftSeed` with its salt, as `ForgeUpgradesSeed`) below it; Tempting Fate's only when `Item.Residue` is above
	 * `TemptingFateResidueAbove`.
	 */
	static FCataclysmForgeKeeping KeepingOn(const FCataclysmItem& Item, float LowTierChance, float RareChance,
											float ReturnAllChance, int32 WalkSeedMask, int32 Day,
											const UDataTable* MaterialTable);

	/**
	 * The forge screen's words for what the empire tree will keep of a craft's materials, said after the craft's question
	 * (N4): "" when nothing is kept; otherwise " The empire tree keeps 2 Upgrade Stone +3 (Material Conservation)."
	 */
	static FString KeptText(const FCataclysmForgeKeeping& Keeping, const TArray<FCataclysmForgeMaterialTake>& Materials);

	// ---- #2741's crafting group, slices C1 and C2: the empire tree's residue and day nodes (`FCataclysmForgeCosts`).

	/** Limited Knowledge's "on items below +6": an item's gear level before the craft must be below this. */
	static constexpr int32 LimitedKnowledgeBelowLevel = 6;

	/** Heat Shielding's "using T3+ materials": the craft's own material's tier must be at least this. */
	static constexpr int32 HeatShieldingLowestTier = 3;

	/** Ancient Techniques' "crafts with 4 or more affixes" (slice C6, R2): the item's affixes before the craft must be at
	 *  least this; an enchantment is not an affix. */
	static constexpr int32 AncientTechniquesLowestAffixes = 4;

	/** Master's Touch's "per 100 CR": its chance counts each whole step of this much residue before the craft. */
	static constexpr float MastersTouchResidueStep = 100.0f;

	/** The salt of Master's Touch's draw: "FMTC". */
	static constexpr int32 MastersTouchSalt = 0x464D5443;

	/**
	 * Whether Master's Touch's draw lands for a craft on `Item` on `Day`: `ChancePerStep` for each whole
	 * `MastersTouchResidueStep` of the item's residue before the craft, and a draw on its own seed below it.
	 */
	static bool ResidueResetLands(const FCataclysmItem& Item, float ChancePerStep, int32 WalkSeedMask, int32 Day);

	/** A craft's days after the empire tree's day nodes: `Days` times `DaysFactor`, less `DaysOff`, rounded; at least
	 *  one when `Days` is at least one, and none when it is none (the coordinating session's ruling of 2026-11-25). */
	static int32 TreeDaysFor(int32 Days, const FCataclysmForgeCosts& Costs);

	/**
	 * The forge screen's words for what the empire tree's crafting nodes changed in a craft, said after the options'
	 * sentences: "" when nothing; otherwise " The empire tree's crafting nodes: residue 40 to 49.5, not 55; 3 days, not
	 * 5." Master's Touch's reset says " Master's Touch: residue 640 to 0, not 690."
	 */
	static FString CostsText(const FCataclysmForgeOptionEffect& Effect, float ResidueBefore);

	/** The same for a cleanse, after its question. */
	static FString CleanseCostsText(const FCataclysmForgeCleanse& Plan);

	// ---- #2741's crafting group, slices C3 and C4: the empire tree's affix nodes (`FCataclysmForgeRolls`,
	// `FCataclysmForgeAffixNodes`). Each drawn as F6b's are, from the item and the day on a salt of its own, so a reload
	// cannot draw again; what they did is not shown before the press, only their chances (G3, as F6's R8).

	/** Precision Crafting's "top 25% of their range": a roll on its draw is drawn from this up. */
	static constexpr float TopQuarterFloor = 0.75f;

	/** The salts of the four draws: "FPRC" (Precision Crafting), "FRDR" (Rough Draft), "FAPS" (Apprentice Smith) and
	 *  "FQMT" (Quality Materials). */
	static constexpr int32 PrecisionCraftingSalt = 0x46505243;
	static constexpr int32 RoughDraftSalt = 0x46524452;
	static constexpr int32 ApprenticeSmithSalt = 0x46415053;
	static constexpr int32 QualityMaterialsSalt = 0x46514D54;

	/**
	 * The three affix nodes on `Changed`, the item a craft on `Before` changed, on `Day`: Rough Draft, then Apprentice
	 * Smith, then Quality Materials (D1), each a chance above nought and a draw on `AffixCraftSeed` of `Before` with its
	 * salt below it, then its choice on the same stream. Rough Draft draws its affix as `AffixShiftOf` draws one, on the
	 * side a drop would pick when both have room (`UCataclysmItemValues::PrefixesPerPiece`, `SuffixesPerPiece`), at
	 * `UCataclysmDropRoll::MaxAffixTierOnADrop(DifficultyTier)` (D2). A node with nothing to act on does nothing.
	 */
	static FCataclysmForgeAffixNodesDone AffixNodesOn(FCataclysmItem& Changed, const FCataclysmItem& Before,
													  const FCataclysmForgeAffixNodes& Nodes, int32 WalkSeedMask,
													  int32 Day, const UDataTable* AffixTable,
													  const UDataTable* BaseTable, int32 DifficultyTier);

	/**
	 * The forge screen's words for the affix nodes held, said at the end of every priced question (G3): "" with none;
	 * otherwise " The empire tree's affix nodes: a 40% chance of one more affix; a 10% chance one affix rises a tier; a
	 * 100% chance one affix's roll is made its best." Only the nodes held are named.
	 */
	static FString AffixNodesChanceText(const FCataclysmForgeAffixNodes& Nodes);

	/** The same for the roll nodes, after the question of a craft that draws a roll: "" with neither; otherwise " Its
	 *  roll is drawn from 30 percent up, with a 45% chance from 75 percent up." */
	static FString RollsText(const FCataclysmForgeRolls& Rolls);

	/** What the affix nodes did, in their order, for the play loop line: "added Flat armor at tier 5; raised Flat evasion
	 *  from tier 2 to tier 3; made Flat life's roll its best, 37 to 100 percent". Empty when they did nothing. */
	static FString AffixNodesDoneText(const FCataclysmForgeAffixNodesDone& Done, const UDataTable* AffixTable);

	/**
	 * The forge screen's words for a price the empire tree discounts, said after the craft's question: "" with no
	 * discount; otherwise " The empire tree's forge nodes take 28% off its gold." and, when Forge Upgrades' tenth landed,
	 * " Forge Upgrades' tenth is in it." The price the question names is already the discounted one.
	 */
	static FString DiscountText(const FCataclysmForgePricing& Pricing, FName Material);

	/**
	 * How many gear levels an upgrade of the item to `ToLevel` adds: 2 when `Chance` is above nought, `ToLevel` is below
	 * `UCataclysmItemValues::MaxGearLevel` and a draw on `AffixCraftSeed` with `SoulForgeSalt` and the item's gear level
	 * is below `Chance`; otherwise 1 (R7: at +9 the draw is not made). Only the upgrade improves an item's level (R6).
	 */
	static int32 SoulForgeLevels(const FCataclysmItem& Item, int32 ToLevel, float Chance, int32 WalkSeedMask, int32 Day);

	/**
	 * Whether an enchantment craft on the item is a critical success: `Chance` above nought and a draw on
	 * `AffixCraftSeed` with `EnchantCriticalSalt` and `Index` below it. `Index` is the craft's own: an imprint's is the
	 * item's pair count, a change's the pair it changes, as their own draws use. `OutStream` is that stream after the
	 * draw, which `LuckyRolls` draws from. "Enchanting" is slice F3's two crafts only (R5).
	 */
	static bool EnchantCritical(const FCataclysmItem& Item, int32 Index, float Chance, int32 WalkSeedMask, int32 Day,
								FRandomStream& OutStream);

	/**
	 * A critical success's rolls, R1, "Lucky": each roll drawn a second time from `Stream` and the better kept, the
	 * higher positive and the lower negative, so every value stays inside its row's range. Better for every row, because a
	 * roll of 1 gives a benefit its best figure and a drawback its harshest (`FCataclysmEnchantmentRow::RollsDown`, ruled
	 * 2026-10-05). Path of Exile's "Lucky things are rolled twice and the best result is used" (poedb.tw/us/Lucky). A
	 * change of drawback keeps its positive (`bPositiveToo` false) and applies the same to its new negative.
	 */
	static FCataclysmRolledEnchantment LuckyRolls(const FCataclysmRolledEnchantment& Drawn, bool bPositiveToo,
												  FRandomStream& Stream);

	/**
	 * What a first press adds when an option is held (R8: the chance, not the outcome): " The Soul Forge: a 15% chance it
	 * rises two levels instead of one." for an upgrade, " The Master Tinkerer: a 5% chance of a critical success, its
	 * rolls drawn twice with the better kept, taking no days." for an enchantment craft. Empty for a chance of nought.
	 */
	static FString MilestoneChanceText(float Chance, bool bUpgrade);

	//~ The residue tools. Issue #47, slice F5, ruled by the coordinating session on 2026-11-22.
	//
	// THE CRAFTING SHEET'S OPERATIONS ROWS, `game/Data/ForgeOperations.csv`: "Reduce CR Gain (1 Craft) | Chaos Stabilizer |
	// -50% CR gain | 0", "Accelerate Craft | Tainted Shard | 0 CR | -1", "Removes the cumulative time penalty (-1 Day per
	// stack)", and "CR Cleanse (Partial) | Purified Essence | -50% CR | 1 day per 500 CR", each at Base Gold 250.
	//
	// THE STABILIZER AND THE SHARDS ARE OPTIONS ON AN ITEM CRAFT'S OWN PRESS (Q23): nothing is saved for a "next craft".
	// The Stabilizer halves the residue the craft adds, certainly (Q27); the craft's days and gold, read from the residue
	// before it, do not change. Each Shard removes one day of the residue's time penalty, `CR / 100` rounded down, down to
	// the craft's base days and never below them (Q24, G2): never an upgrade's level days. Each option charges its row's
	// Base Gold through `GoldFor` at the item's residue, the Stabilizer once and the Shards once per shard (G1).
	//
	// THE CLEANSE IS A CRAFT OF ITS OWN: half the item's residue, one day for each 500 residue or part of it and at least
	// one (Q25). It never lifts a Consumption mark already made (Q26): the design's "Only killing the double clears
	// residue", and `ACataclysmPlayerState::ClearConsumptionMark` is the double's death's.

	/** The rows of the three materials: `game/Data/CraftingMaterials.csv`. */
	static const TCHAR* ChaosStabilizerRow;
	static const TCHAR* TaintedShardRow;
	static const TCHAR* PurifiedEssenceRow;

	/** The three operations' rows in `game/Data/ForgeOperations.csv`, whose Base Gold `BaseGoldOf` reads. */
	static const TCHAR* ReduceResidueGainOperationRow;
	static const TCHAR* AccelerateCraftOperationRow;
	static const TCHAR* CleanseOperationRow;

	/** The share of a craft's residue a Chaos Stabilizer lets through: "-50% CR gain". */
	static constexpr float StabilizedResidueShare = 0.5f;

	/** The days one Tainted Shard removes: "-1 Day per stack". */
	static constexpr int32 DaysPerTaintedShard = 1;

	/** The share of an item's residue a cleanse leaves: "-50% CR". */
	static constexpr float CleansedResidueShare = 0.5f;

	/** The residue each day of a cleanse takes: "1 day per 500 CR". */
	static constexpr float ResiduePerCleanseDay = 500.0f;

	/** The residue's time penalty in days at a residue: `CR / 100`, rounded down. What the Tainted Shards can remove. */
	static int32 PenaltyDaysFor(float Residue);

	/**
	 * What the options do to one item craft whose plan takes the item from `ResidueBefore` to `ResidueAfter` in `Days`
	 * for `CraftGold`, against the Stabilizers, the Shards and the gold held.
	 *
	 * REFUSED for a Stabilizer when none is held or the craft adds no residue; for Shards when fewer are held than asked,
	 * when the craft has no penalty day, or when more are asked than its penalty days ("This craft has 2 days of the
	 * residue's penalty, so at most 2 Tainted Shards can be used."); and for the gold, the craft's and the options'
	 * together.
	 */
	static FCataclysmForgeOptionEffect OptionsOn(float ResidueBefore, float ResidueAfter, int32 Days, int64 CraftGold,
												 const FCataclysmForgeCraftOptions& Options, int32 StabilizersHeld,
												 int32 ShardsHeld, int32 DifficultyTier, int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing(),
		const FCataclysmForgeCosts& Costs = FCataclysmForgeCosts());

	/**
	 * What the options add to a craft's first-press question, empty with none. An added socket at 250 residue, tier 1,
	 * with both: " With a Chaos Stabilizer: residue 250 to 257.5, not 265. With 1 Tainted Shard: 4 days, not 5. The
	 * options cost 3,000 gold more."
	 */
	static FString OptionsQuestion(const FCataclysmForgeOptionEffect& Effect, float ResidueBefore, float PlanResidueAfter,
								   int32 PlanDays);

	/**
	 * What halving the item's residue takes, against the Purified Essence held.
	 *
	 * REFUSED for no item, an item carrying no residue, no Purified Essence, and too little gold.
	 */
	static FCataclysmForgeCleanse CleanseOf(const FCataclysmItem& Item, int32 EssenceHeld, int32 DifficultyTier,
											int64 GoldHeld,
		const FCataclysmForgePricing& Pricing = FCataclysmForgePricing(),
		const FCataclysmForgeCosts& Costs = FCataclysmForgeCosts());

	/**
	 * What the first press of a cleanse says, at tier 1: "Press again to cleanse half the residue: one Purified Essence,
	 * 6,250 gold, residue 1200 to 600, 3 days of the run."
	 */
	static FString CleanseQuestion(const FCataclysmForgeCleanse& Plan);

	//~ Dismantling a carried piece. Issue #2848, slice S2, ruled by the coordinating session on 2026-11-22: the plan of
	//~ selling and dismantling (sharp-meitner-246682/.claude/windows/plan_selling_and_dismantling.md), sections 6 to 8,
	//~ and D1 to D5.
	//
	// A DISMANTLE IS INSTANT (D2, derived from the owner's O2, "No days (Recommended)", asked of "selling an item or
	// dismantling it at the Forge"): no gold, no days, no residue, no material taken. It destroys the piece and gives:
	//   - Schematic Fragments by rarity (N4), Dismantling Dust a socket (N5) and a base upgrade stone yield by rarity
	//     (the ruling of section 8, after the owner's "shattering dropped items should still give you stones"), each
	//     raised by Salvage Protocol and Disassembly Expert;
	//   - the socketed gems, back to the bag;
	//   - the refund, one stone of each level the piece was raised through, which no node raises.

	/** N4: the Schematic Fragments a dismantle gives, by rarity: 0.06 to Masterful, then 0.1, 0.2, 0.4, 0.8. */
	static float DismantleFragmentsFor(ECataclysmRarity Rarity);

	/** Section 8 (a): the base upgrade stones a dismantle gives, by rarity: 0.025 to Masterful, then doubling to 0.4. */
	static float DismantleStonesFor(ECataclysmRarity Rarity);

	/** N5: the Dismantling Dust a dismantle gives for each socket the piece has. */
	static constexpr float DismantleDustPerSocket = 0.02f;

	/** N6: Salvage Protocol's increase a point on the drawn yields, 25% at its 10 points. */
	static constexpr float SalvageProtocolIncreasePerPoint = 0.025f;

	/** N7: Disassembly Expert's increase on the drawn yields of a piece of Superb or above (O4: this game has no Rare). */
	static constexpr float DisassemblyExpertIncrease = 0.20f;

	/** The lowest rarity Disassembly Expert raises: Superb (O4). */
	static constexpr ECataclysmRarity DisassemblyExpertLowestRarity = ECataclysmRarity::Superb;

	/** The salt a dismantle's draw mixes into the run's seed through `AffixCraftSeed`: "DISM" (D5). */
	static constexpr int32 DismantleSalt = 0x4449534D;

	/**
	 * The two nodes' increase on a piece of `Rarity`: `SalvageIncrease` (Salvage Protocol's, its points times
	 * `SalvageProtocolIncreasePerPoint`) and, from Superb up, `DisassemblyIncrease`. ADDED, as increases are in the
	 * damage pipeline's one bucket: both nodes say "more crafting materials" of one yield, and neither is a "more".
	 */
	static float DismantleIncreaseFor(ECataclysmRarity Rarity, float SalvageIncrease, float DisassemblyIncrease);

	/**
	 * What dismantling the item gives, before the draw: its rarity, each drawn yield's expected count raised by
	 * `Increase`, the refund and the gems. `GearRarityTable` gives the gate a piece saved before `GearLevelAtDrop` existed
	 * is read at.
	 *
	 * REFUSED for no item, the weapon made at creation (the game gives it back its type on every load and it is the
	 * character's own; a judgement, as slice F4 refuses it its crafts), a piece with no rarity, and no gear rarity table.
	 */
	static FCataclysmForgeDismantle DismantleOf(const FCataclysmItem& Item, float SalvageIncrease, float DisassemblyIncrease,
												const UDataTable* GearRarityTable);

	/**
	 * Draws a plan's fractional yields on `Stream` (D5), in this order: the fragments, the Dust, the count of base
	 * stones, then for each stone its material tier (`UCataclysmDropRoll::RollMaterialTier` with `MagicFind`, D3) and
	 * which of that tier's two stones, evenly, held at `MaxUpgradeStoneOnADrop(StoneTier)`. `StoneTier` is the piece's
	 * `TierAtDrop`, or the run's tier when that is unknown.
	 */
	static void DrawDismantle(FCataclysmForgeDismantle& Plan, const UDataTable* MaterialTierTable, float MagicFind,
							  int32 StoneTier, FRandomStream& Stream);

	/** One expected yield drawn: the whole part always, and one more with the fraction's chance (D5). */
	static int32 DrawWholeAndFraction(float Expected, FRandomStream& Stream);

	/** The tier a dismantle's base stones are capped at: the piece's `TierAtDrop`, or `RunTier` when that is 0. */
	static int32 DismantleStoneTier(const FCataclysmItem& Item, int32 RunTier);

	/**
	 * What the first press says, `Named` the piece's name: "Press again to dismantle Masterful Helm of Warding: it is
	 * destroyed, and gives back 1 gem and 4 upgrade stones (+1, +2, +3, +4), and about 0.06 Schematic Fragments, 0.02
	 * Dismantling Dust and 0.03 upgrade stones drawn at random. No gold, no days."
	 */
	static FString DismantleQuestion(const FCataclysmForgeDismantle& Plan, const FString& Named);

	/** What a done dismantle gave, for the screen and the play log: "2 Schematic Fragments, 1 Upgrade Stone +3, ...". */
	static FString DismantleGiven(const FCataclysmForgeDismantle& Done);
};
