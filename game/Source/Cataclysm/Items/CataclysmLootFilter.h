// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmItem.h"
#include "CataclysmLootFilter.generated.h"

class AActor;
class ACataclysmDroppedItem;
class UDataTable;

/**
 * THE LOOT FILTER: rules that hide a drop's name on the floor or change how it is drawn. Issue #2846, slice LF1.
 *
 * THE OWNER'S RULINGS OF 2026-11-22, each the survey's recommendation:
 *   - O1 and O2: a rule matches rarity, the kind of drop or a gear slot, gear level (+0 to +10), specific affixes with a
 *     minimum tier and count, having an enchantment, socket count, a weapon damage type, a material's tier, a gem's
 *     rarity, and -- ruled (a) the same day by the coordinating session, a judgement -- "rarity at least N rungs below
 *     the current tier's rarity". It shows, hides, or restyles the name's text colour, background colour and size. No
 *     sound and no beam: the project has no sound files (#2817) and no beam effect. THE BORDER STAYS THE RARITY MARKER
 *     (#718): a restyle never changes the border's width or colour.
 *   - O3: a hidden drop stays on the floor; holding Alt shows it, and while shown it can be taken.
 *   - O4: filters are kept in a preferences file of their own, outside the save records (slice LF2; this slice keeps
 *     the active filter in the game instance only).
 *
 * WHAT THE GENRE SETTLES, recorded in docs/DECISIONS.md with its sources: every condition inside one rule must match
 * (Path of Exile's item filter reference: "all of them must be matched"; Last Epoch: "all conditions must be met"), and
 * the rule highest in the list that matches decides (Last Epoch: "Rules that are higher on the filter take priority").
 *
 * WHY THE CONDITIONS ARE FIELDS OF ONE STRUCT rather than a list of condition objects: every condition is optional and
 * at most one of each kind is useful in a rule, which is how Last Epoch's rule screen offers them ("a rule can hold only
 * one Item Type condition"). A field left at its "any" value does not narrow the rule. Slice LF2 writes the struct to
 * the preferences file with the engine's JSON converter, which reads UPROPERTY fields.
 */

/** What a matching rule does to a drop. */
UENUM(BlueprintType)
enum class ECataclysmLootFilterAction : uint8
{
	Show		UMETA(DisplayName = "Show"),
	Hide		UMETA(DisplayName = "Hide"),
	Restyle		UMETA(DisplayName = "Restyle"),
};

/** Which kind of drop a rule is about. `Any` matches all four. */
UENUM(BlueprintType)
enum class ECataclysmLootKind : uint8
{
	Any			UMETA(DisplayName = "Any"),
	Gear		UMETA(DisplayName = "Gear"),
	Material	UMETA(DisplayName = "Crafting material"),
	Gold		UMETA(DisplayName = "Gold"),
	Gem			UMETA(DisplayName = "Gems"),
};

/** A yes, no or either answer, for "has an enchantment". */
UENUM(BlueprintType)
enum class ECataclysmLootTriState : uint8
{
	Either	UMETA(DisplayName = "Either"),
	Yes		UMETA(DisplayName = "Yes"),
	No		UMETA(DisplayName = "No"),
};

/**
 * One rule: conditions that must all match, and what happens when they do.
 *
 * EVERY CONDITION'S DEFAULT MATCHES EVERYTHING, so a rule with only an action matches every drop, and a rule written
 * with one condition is narrowed by that one alone.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmLootFilterRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	ECataclysmLootFilterAction Action = ECataclysmLootFilterAction::Show;

	// -- Conditions ------------------------------------------------------

	/** Gear, a material, gold or gems; Any for all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	ECataclysmLootKind Kind = ECataclysmLootKind::Any;

	/**
	 * The rarities that match: gear's rarity, or a gem's. Empty matches every rarity. A material and gold have no rarity,
	 * so a rule that lists any rarity never matches them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	TArray<ECataclysmRarity> Rarities;

	/**
	 * "Rarity at least N rungs below the current tier's rarity" (ruled (a), 2026-11-22). A condition a player's own rule
	 * may use; no built-in filter uses it since the owner's rule of the same day replaced "Regular". The current tier's rarity is the
	 * rarity the design's reference character wears at that difficulty tier (section IV's tier table): Everyday at tier
	 * 1 through Cataclysmic at tier 8. 0 means the condition is not used. Never matches a material or gold.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 RungsBelowTierAtLeast = 0;

	/**
	 * "Rarity below the rarity of what is worn in the same slot": the owner's rule of 2026-11-22. When set, a piece of
	 * gear matches only when its rarity is STRICTLY below the rarity worn in the slot it would fill
	 * (`FCataclysmLootContext::WornRarity`). A slot with nothing worn in it matches nothing. Never matches a material,
	 * gold or gems.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	bool bBelowWornRarity = false;

	/**
	 * The gear slots that match, as the ItemBases table's `Slot` names them: "Head", "Chest", ... "Weapon". Empty
	 * matches every slot. Never matches a material, gold or gems.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	TArray<FString> Slots;

	/** Gear level, the +0 to +10 upgrade level, inclusive. 0 and 10 match every piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MinGearLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MaxGearLevel = 10;

	/**
	 * Affixes, by their row in the Affixes table. Empty means the condition is not used. When it is used, the piece must
	 * carry at least `MinAffixCount` of them, each at `MinAffixTier` or above: Last Epoch's "With at least 2 of these
	 * affixes" and its per-affix tier, and Path of Exile's `HasExplicitMod >= 2`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	TArray<FName> Affixes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MinAffixTier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MinAffixCount = 1;

	/** Whether the piece must carry an enchantment, must not, or either. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	ECataclysmLootTriState HasEnchantment = ECataclysmLootTriState::Either;

	/** The number of sockets, inclusive. 0 and `MaxSocketsMatched` match every piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MinSockets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MaxSockets = 6;

	/** Weapon damage types, as the item carries them ("Fire", "War", ...). Empty means not used; else any one matches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	TArray<FName> DamageTypes;

	/** A material's tier, 1 (Common) to 5 (Extremely Rare), inclusive. Narrows only materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MinMaterialTier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 MaxMaterialTier = 5;

	// -- The restyle, read only when Action is Restyle --------------------

	/** An index into `UCataclysmLootFilterRules::PaletteColour`, or INDEX_NONE to keep the name's own colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 TextColour = INDEX_NONE;

	/** An index into the same palette, or INDEX_NONE for no background, which is how every name is drawn today. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 BackgroundColour = INDEX_NONE;

	/** An index into `UCataclysmLootFilterRules::Sizes`: 0 is the size every name has today. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	int32 Size = 0;
};

/** A filter: a name and its rules, highest priority first. */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmLootFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Loot Filter")
	TArray<FCataclysmLootFilterRule> Rules;
};

/**
 * What the filter needs to know about one drop, read once from it. Plain values, so every rule can be tested without a
 * world, a drop actor or a data table.
 */
USTRUCT()
struct CATACLYSM_API FCataclysmLootFacts
{
	GENERATED_BODY()

	ECataclysmLootKind Kind = ECataclysmLootKind::Gear;

	/** Gear's rarity, or a gem's. Not read for a material or gold. */
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;

	/** The base's slot, "Head" ... "Weapon"; empty for anything that is not gear or a base not found. */
	FString Slot;

	int32 GearLevel = 0;
	TArray<FCataclysmRolledAffix> Affixes;
	int32 EnchantmentCount = 0;
	int32 SocketCount = 0;
	TArray<FName> DamageTypes;

	/** 1 to 5 for a material; 0 otherwise. */
	int32 MaterialTier = 0;
};

/**
 * What the rules compare a drop with that is not the drop: the run's difficulty tier, and the rarity worn in each slot.
 */
USTRUCT()
struct CATACLYSM_API FCataclysmLootContext
{
	GENERATED_BODY()

	/** The difficulty tier being played, 1 to 8. */
	int32 DifficultyTier = 1;

	/**
	 * The rarity worn in each base slot ("Head" ... "Weapon"), as `UCataclysmLootFilterRules::WornRaritiesOf` reads it. A
	 * slot that is absent has nothing worn in it, and `bBelowWornRarity` then hides nothing for it.
	 */
	TMap<FString, ECataclysmRarity> WornRarity;
};

/** What the filter decided for one drop: whether its name is hidden, and how it is drawn when it is shown. */
USTRUCT()
struct CATACLYSM_API FCataclysmLootVerdict
{
	GENERATED_BODY()

	bool bHidden = false;

	/** The palette index for the text, or INDEX_NONE for the name's own colour. */
	int32 TextColour = INDEX_NONE;

	/** The palette index for a background, or INDEX_NONE for none. */
	int32 BackgroundColour = INDEX_NONE;

	/** The scale the name is drawn at: one of `UCataclysmLootFilterRules::Sizes`. */
	float Scale = 1.0f;

	/** The index of the rule that decided, or INDEX_NONE when none matched and the drop is shown as it always was. */
	int32 Rule = INDEX_NONE;
};

/**
 * Matching drops against a filter, the three built-in filters, and the restyle palette. Static functions over plain
 * values, so every judgement is testable without a world; `VisibleDrops` is the one place a world's drops meet a filter.
 */
UCLASS()
class CATACLYSM_API UCataclysmLootFilterRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The most rules one filter may hold. N1, ruled 2026-11-22: Last Epoch's "up to 75 rules". */
	static constexpr int32 MaxRules = 75;

	/** The sizes a restyle may choose, as a scale of the size every name has. N5, ruled 2026-11-22, a judgement. */
	static constexpr int32 SizeCount = 3;
	static const float Sizes[SizeCount];

	/**
	 * The restyle palette: indices 0 to 7 are the eight rarity colours, Everyday to Cataclysmic, read from the gear
	 * rarity table, and 8 to 11 are four more. N4, ruled 2026-11-22, a judgement. Twelve entries.
	 */
	static constexpr int32 PaletteSize = 12;

	/** The four colours after the rarities: yellow, cyan, pink and black. A judgement, to be tuned by eye. */
	static const FLinearColor ExtraColours[4];

	/**
	 * The colour of palette entry `Index`. A rarity's colour is read from `GearRarityTable`, as a name's own colour is;
	 * white when the table is missing. White for an index outside the palette.
	 */
	static FLinearColor PaletteColour(int32 Index, const UDataTable* GearRarityTable);

	/**
	 * How strongly a hidden drop's name is drawn while Alt shows it: its colours at this alpha, so a player can tell a
	 * shown drop from one the filter hides. A judgement, to be tuned by eye.
	 */
	static constexpr float RevealedAlpha = 0.5f;

	/**
	 * The rarity the design's reference character wears at a difficulty tier, section IV's tier table: Everyday at tier 1
	 * through Cataclysmic at tier 8. A tier outside 1 to 8 is held to that range.
	 */
	static ECataclysmRarity TierRarity(int32 DifficultyTier);

	/** Whether every condition of `Rule` holds for a drop with these facts, in this context. */
	static bool Matches(const FCataclysmLootFilterRule& Rule, const FCataclysmLootFacts& Facts,
						const FCataclysmLootContext& Context);

	/**
	 * What `Filter` decides for a drop with these facts: the highest rule that matches decides, and a drop no rule
	 * matches is shown as it always was.
	 *
	 * A CRAFTING MATERIAL, GOLD OR GEMS ARE NEVER HIDDEN, a judgement of the survey that the ruling did not change: they
	 * come to the character within fifteen metres whether named or not, so hiding one would remove only the sight of it
	 * being collected. A Hide rule that matches one shows it unstyled. They can be restyled.
	 *
	 * ONLY THE FIRST `MaxRules` RULES ARE READ, so a filter written past the limit behaves as its first 75.
	 */
	static FCataclysmLootVerdict VerdictFor(const FCataclysmLootFilter& Filter, const FCataclysmLootFacts& Facts,
											const FCataclysmLootContext& Context);

	/**
	 * The rarity worn in each base slot, for `FCataclysmLootContext::WornRarity`. The owner's rule and the coordinating
	 * session's settlements of 2026-11-22:
	 *   - one slot, one item: that item's rarity;
	 *   - "Ring": the LOWEST rarity of the rings worn, and nothing when any ring slot is empty ("Rings would be the
	 *     only exception as it would have to be based off your lowest ring tier"; an empty slot hides nothing);
	 *   - "Weapon": the same, the lower of the two hands; one two-handed weapon fills both hands
	 *     (`TwoHandedOccupiesBothWeaponSlots`, which leaves the second slot empty and blocked), so its rarity decides.
	 * A slot whose candidates hold nothing is left out of the map.
	 *
	 * READ FROM WHAT IS WORN NOW, by the heads-up display on every frame, so a change of gear changes which names are
	 * drawn on the next frame with no other hook.
	 *
	 * @param Worn                 every slot's item, or null where nothing is worn, indexed by `ECataclysmGearSlot`
	 * @param bTwoHandedInBothHands  `UCataclysmEquipmentComponent::TwoHandedOccupiesBothWeaponSlots`
	 */
	static TMap<FString, ECataclysmRarity> WornRaritiesOf(const TArray<const FCataclysmItem*>& Worn,
														  bool bTwoHandedInBothHands);

	/**
	 * The context the filter reads for a character: the difficulty tier of its world (`ACataclysmGameMode::
	 * DifficultyTierIn`) and the rarities it wears (`WornRaritiesOf`, from its `UCataclysmEquipmentComponent`). A
	 * character with no equipment component wears nothing, so the worn-rarity condition hides nothing for it.
	 */
	static FCataclysmLootContext ContextOf(const AActor* Character);

	/** The facts of a drop: its kind, rarity, slot (from `BaseTable`), gear level, affixes and the rest. */
	static FCataclysmLootFacts FactsOf(const ACataclysmDroppedItem& Drop, const UDataTable* BaseTable);

	/**
	 * Of the drops whose names would be drawn (`UCataclysmDropPickup::DropsToName`), the ones this filter leaves visible,
	 * each with its verdict, in the same order. A hidden drop is left out unless `bRevealHidden`, the Alt key held; then
	 * it is kept, with its verdict still saying hidden, so the name can be drawn faintly.
	 *
	 * WHAT IS LEFT OUT CANNOT BE CLICKED OR HOVERED, with no other change: `ACataclysmHUD::DropUnderPoint` tests a click
	 * only against the names `DrawDropNames` drew, and the ground hover goes through it.
	 *
	 * @param OutDrops     emptied first
	 * @param OutVerdicts  emptied first; index N is the verdict of OutDrops[N]
	 */
	static void VisibleDrops(const TArray<ACataclysmDroppedItem*>& Named, const FCataclysmLootFilter& Filter,
							 const FCataclysmLootContext& Context, bool bRevealHidden, const UDataTable* BaseTable,
							 TArray<ACataclysmDroppedItem*>& OutDrops, TArray<FCataclysmLootVerdict>& OutVerdicts);

	// -- The three built-in filters (N2, ruled 2026-11-22) -----------------

	/** "Everything": no rules, every drop shown as it always was. */
	static FCataclysmLootFilter Everything();

	/**
	 * "Hide worse than worn": hides gear whose rarity is strictly below what is worn in the slot it would fill. THE
	 * FILTER A NEW PLAYER STARTS WITH. The owner, 2026-11-22: "What if we hid everything that was below the tier of the
	 * currently equipped slot? So for instance, if you had a superb helm, it would hide trash and quality helms." It
	 * replaces the "Regular" built-in, which was never written.
	 */
	static FCataclysmLootFilter HideWorseThanWorn();

	/**
	 * "Strict": hides all gear below Superb (N7, ruled 2026-11-22), AND what "Hide worse than worn" hides. A judgement of
	 * this slice: a character wearing Legendary rings would otherwise see Masterful rings under Strict that the default
	 * filter hides, and Strict would be the less strict of the two.
	 */
	static FCataclysmLootFilter Strict();

	/** The name of the filter a new player starts with: `HideWorseThanWorn`'s. */
	static const TCHAR* DefaultFilterName;

	/** The built-in filter of this name, ignoring case; false when there is none. */
	static bool BuiltInNamed(const FString& Name, FCataclysmLootFilter& OutFilter);
};
