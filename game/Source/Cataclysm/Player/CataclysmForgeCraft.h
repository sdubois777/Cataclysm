// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "CataclysmForgeCraft.generated.h"

class ACataclysmPlayerCharacter;
class UCataclysmEmpireRun;
class UCataclysmInventoryComponent;

/** Where the item an upgrade raises is: one of the character's carried cells, or one of its worn gear slots. */
struct FCataclysmForgeTarget
{
	bool bWorn = false;
	int32 CarriedSlot = INDEX_NONE;
	ECataclysmGearSlot WornSlot = ECataclysmGearSlot::Count;

	static FCataclysmForgeTarget Carried(int32 Slot)
	{
		FCataclysmForgeTarget Out;
		Out.CarriedSlot = Slot;
		return Out;
	}

	static FCataclysmForgeTarget Worn(ECataclysmGearSlot Slot)
	{
		FCataclysmForgeTarget Out;
		Out.bWorn = true;
		Out.WornSlot = Slot;
		return Out;
	}

	bool operator==(const FCataclysmForgeTarget& Other) const
	{
		return bWorn == Other.bWorn && (bWorn ? WornSlot == Other.WornSlot : CarriedSlot == Other.CarriedSlot);
	}
};

/** What an upgrade press did. */
enum class ECataclysmForgeResult : uint8
{
	/** The item was raised, the stones taken and the days passed. */
	Done,

	/** Nothing changed; the message says why. */
	Refused,

	/** Nothing changed: the upgrade of a worn piece would cross the Consumption Threshold, and the message asks. */
	AsksAboutTheThreshold,
};

/**
 * Everything an item craft takes and costs, decided by its plan and its options before anything moves: the one shape
 * every item craft hands `UCataclysmForgeCraft::DoItemCraft`. Issue #47, slice F5.
 */
struct FCataclysmForgeCraftStep
{
	FCataclysmForgeTarget Target;

	/** The craft's own material, then each option's, each taken through `TakeMaterials`. */
	TArray<FCataclysmForgeMaterialTake> Materials;

	/** The craft's gold and its options', paid once. */
	int64 Gold = 0;

	/** The item's residue after the craft and its options; the residue before is the item's own. */
	float ResidueAfter = 0.0f;

	/** The days of the run it costs, after the options. */
	int32 Days = 0;

	/** What the options did, for the line that names them; none used is no line. */
	FCataclysmForgeOptionEffect Options;

	/** The threshold question's first words: "Upgrading", "Socketing a gem into". */
	FString ThresholdWords;

	/** Whether the empire tree's affix nodes act on the changed item (#2741's crafting slices C3 and C4): every item craft
	 *  but the dismantle, which ends the piece (G1). */
	bool bTreeAffixNodes = true;

	/** The refusals with no run, and with nothing at the target. */
	FString NoRunRefusal = TEXT("There is no run to take the craft's days from.");
	FString NothingThereRefusal = TEXT("There is nothing there to craft.");
};

/** The parts only some crafts add to the shared sequence; socketing's gem. Each may be left unset. #47, F5. */
struct FCataclysmForgeCraftHooks
{
	/** Taken after the materials and before the item changes; false refuses with `TakeRefusal`. */
	TFunction<bool()> TakeBeforeTheChange;
	FString TakeRefusal;

	/** Puts back what `TakeBeforeTheChange` took, when a later step fails. */
	TFunction<void()> GiveBackWhatWasTaken;

	/** Done after the item is put back; false puts the item, the materials and the gold back with `AfterRefusal`. */
	TFunction<bool()> AfterTheChange;
	FString AfterRefusal;
};

/**
 * The forge's two crafts, done to the character playing in a world: raising an item a gear level with two upgrade
 * stones, and combining two stones of a level into one of the next. The owner's ruling of 2026-11-19, "build it
 * next"; the rules are `UCataclysmForge`'s, in `game/Source/Cataclysm/Items/CataclysmForge.h`.
 *
 * WHERE, AND WHEN. The design puts the forge at the Smith in the capital, between walks: "All capital services cost
 * time." The walkable capital and its Smith are not built, so the forge's screen opens from the empire map, as the
 * empire upgrade tree's does, and both crafts are REFUSED WHILE A WALK IS UNDER WAY (`WalkRefusal`), the tree's rule
 * and for its reason: days do not pass inside a dungeon.
 *
 * AN UPGRADE, IN ORDER: the stones are taken from the bag; the item, carried or worn, is raised one level in its own
 * cell or slot and carries 25 more residue; then the days pass through `UCataclysmEmpireRun::AdvanceDays`, so a surge
 * or a resolve inside them happens on its own day. A PLAY LOOP line says it, a second says what the days did -- the
 * dungeons resolved, the cities hit and by how much, the cities fallen, any surge (issue #2857) -- and `ForgeCrafted`
 * saves the character record and the run record.
 *
 * A WORN PIECE'S UPGRADE ASKS FIRST when it would take Worn Residue from below this tier's Consumption Threshold to at
 * or past it: "There is no path across the threshold that does not show the warning first." A confirmed crossing
 * marks the character and captures the build its double wears, exactly as wearing a piece across the threshold does
 * (`ACataclysmPlayerCharacter::WearPastTheThreshold`): the mark before the change, the build after it.
 */
UCLASS()
class CATACLYSM_API UCataclysmForgeCraft : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** What both crafts say during a walk. */
	static const TCHAR* WalkRefusal;

	/**
	 * Why the forge cannot be used now, or empty when it can: no world, no character playing, or a walk under way.
	 * An upgrade also needs a run to take its days from, which `Upgrade` asks itself.
	 */
	static FString RefusalNow(const UObject* WorldContext);

	/** The character playing in a world, or null. */
	static ACataclysmPlayerCharacter* CharacterIn(const UObject* WorldContext);

	/** The item at a target on this character, or null for an empty cell, a material's cell or an empty slot. */
	static const FCataclysmItem* ItemAt(const ACataclysmPlayerCharacter& Character, const FCataclysmForgeTarget& Target);

	/** What an upgrade of the item at a target would take now, with the stones the character carries. */
	static FCataclysmForgeUpgrade PlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target);

	/**
	 * Raise the item at a target one gear level.
	 *
	 * @param bConfirmedPastThreshold  the player has answered the threshold question, so do not ask it
	 * @param OutDone                  what the upgrade took, when it was done
	 * @param OutMessage               the refusal, or the threshold question; empty when done
	 */
	static ECataclysmForgeResult Upgrade(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
										 bool bConfirmedPastThreshold, FCataclysmForgeUpgrade& OutDone,
										 FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	/**
	 * Combine two upgrade stones of a level into one of the next. No days, no residue: it changes no item.
	 *
	 * REFUSED, CHANGING NOTHING, during a walk, for a level outside 1 to 9, with fewer than two of that level, and when
	 * the made stone would have nowhere to go -- no stack of it, no free cell, and the two taken leave a stack behind.
	 */
	static bool Combine(const UObject* WorldContext, int32 Level, FCataclysmForgeCombine& OutDone, FString& OutReason);

	/**
	 * Combine two carried gems of `Gem` at `Rarity` into one of the next rarity. No cap, no days, no residue, no gold,
	 * as a stone combine (the owner's ruling of 2026-11-21). Issue #46, slice G6.
	 *
	 * REFUSED, CHANGING NOTHING, during a walk, for the refusals of `UCataclysmForge::GemCombineOf`, and when the made gem
	 * would have nowhere to go -- no stack of it, no free cell, and the two taken leave a stack behind.
	 */
	static bool CombineGems(const UObject* WorldContext, FName Gem, ECataclysmRarity Rarity,
							FCataclysmForgeGemCombine& OutDone, FString& OutReason);

	//~ Socketing and unsocketing a gem. Issue #46, slice G4.

	/** What putting a carried gem into a socket of the item at a target would take now. */
	static FCataclysmForgeSocketing SocketPlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
												  int32 Socket, FName Gem, ECataclysmRarity Rarity);

	/** What taking the gem out of a socket of the item at a target would take now. */
	static FCataclysmForgeSocketing UnsocketPlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													int32 Socket);

	/**
	 * Put a carried gem into a socket of the item at a target, carried or worn: one Jeweler's Setting Agent and the gem
	 * leave the bag, the item carries 5 more residue, and the days pass. A worn piece asks first when it would cross the
	 * Consumption Threshold, as an upgrade does.
	 */
	static ECataclysmForgeResult SocketGem(const UObject* WorldContext, const FCataclysmForgeTarget& Target, int32 Socket,
										   FName Gem, ECataclysmRarity Rarity, bool bConfirmedPastThreshold,
										   FCataclysmForgeSocketing& OutDone, FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	/**
	 * Take the gem out of a socket of the item at a target: one Dismantling Dust leaves the bag, the gem goes back to
	 * it, the item carries 3 more residue, and the days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult UnsocketGem(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											 int32 Socket, bool bConfirmedPastThreshold,
											 FCataclysmForgeSocketing& OutDone, FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	//~ Adding a socket and changing a socket's type. Issue #46, slice G9.

	/**
	 * The most sockets the item's slot takes, `game/Data/ItemSockets.csv` through `UCataclysmDropRoll::MaxSocketsFor`, a
	 * weapon matched on its hands; -1 when its base or the table is not found.
	 */
	static int32 MaxSocketsOf(const FCataclysmItem& Item);

	/** What adding a socket to the item at a target would take now. */
	static FCataclysmForgeSocketWork AddSocketPlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target);

	/** What making a socket of the item at a target a socket of `NewType` would take now. */
	static FCataclysmForgeSocketWork ChangeSocketTypePlanFor(const UObject* WorldContext,
															 const FCataclysmForgeTarget& Target, int32 Socket,
															 ECataclysmSocketType NewType);

	/**
	 * Add a socket to the item at a target, carried or worn: one Shattered Core leaves the bag, the item gains an empty
	 * socket of the type `UCataclysmForge::AddedSocketType` rolls for the run's day, it carries 15 more residue, and the
	 * days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult AddSocket(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
										   bool bConfirmedPastThreshold, FCataclysmForgeSocketWork& OutDone,
										   FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	/**
	 * Make an empty socket of the item at a target a socket of `NewType`: one Focusing Lens leaves the bag, the item
	 * carries 10 more residue, and the days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult ChangeSocketType(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
												  int32 Socket, ECataclysmSocketType NewType,
												  bool bConfirmedPastThreshold, FCataclysmForgeSocketWork& OutDone,
												  FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	//~ The three crafts that change one affix. Issue #47, slice F1.

	/** What one of the three crafts on the affix `Affix` of the item at a target would take now. */
	static FCataclysmForgeAffixWork AffixWorkPlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													 ECataclysmForgeAffixCraft Craft, int32 Affix);

	/**
	 * Reroll, perfect or raise the tier of the affix `Affix` of the item at a target, carried or worn: one of the craft's
	 * material leaves the bag, the affix's roll or tier changes and nothing else of it, the item carries the craft's
	 * residue, and the days pass. A reroll's roll is `UCataclysmForge::RerolledAffixRoll` for the run's day, drawn before
	 * the days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult CraftAffix(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											ECataclysmForgeAffixCraft Craft, int32 Affix, bool bConfirmedPastThreshold,
											FCataclysmForgeAffixWork& OutDone, FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	//~ Adding a chosen affix, and shifting an affix to another stat. Issue #47, slice F2.

	/**
	 * What adding the affix `AffixRow` at `Tier` to the item at a target would take now, its roll drawn for the run's day
	 * (`UCataclysmForge::AffixCraftSeed` with `DeterministicAffixSalt` and the affix's place). With no run the draw is
	 * for day 0 of no mask; the craft itself is then refused.
	 */
	static FCataclysmForgeAffixChange AddAffixPlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													  FName AffixRow, int32 Tier);

	/** What shifting the affix `Affix` of the item at a target would take now, its new stat drawn for the run's day. */
	static FCataclysmForgeAffixChange ShiftAffixPlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
														int32 Affix);

	/**
	 * Add the affix `AffixRow` at `Tier` to the item at a target, carried or worn: Schematic Fragments leave the bag,
	 * the item gains the affix with the roll drawn for the run's day and is one rarity higher, it carries 5 residue a
	 * tier, and the days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult AddAffix(const UObject* WorldContext, const FCataclysmForgeTarget& Target, FName AffixRow,
										  int32 Tier, bool bConfirmedPastThreshold, FCataclysmForgeAffixChange& OutDone,
										  FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	/**
	 * Shift the affix `Affix` of the item at a target to the stat drawn for the run's day, keeping its tier and roll: a
	 * Crystal of Instability leaves the bag, the item carries 15 more residue, and the days pass. Asks about the
	 * threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult ShiftAffix(const UObject* WorldContext, const FCataclysmForgeTarget& Target, int32 Affix,
											bool bConfirmedPastThreshold, FCataclysmForgeAffixChange& OutDone,
											FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	//~ The three crafts that change a weapon's damage types. Issue #47, slice F4.

	/**
	 * What one of the three crafts on the weapon at a target would take now. `Chosen` is the type a specific infusion
	 * adds or the type a change replaces. A drawn type is drawn for the run's day (`UCataclysmForge::AffixCraftSeed` with
	 * the craft's salt), and for day 0 of no mask with no run, when the craft itself is refused.
	 */
	static FCataclysmForgeDamageTypeWork DamageTypeWorkPlanFor(const UObject* WorldContext,
															   const FCataclysmForgeTarget& Target,
															   ECataclysmForgeDamageTypeCraft Craft, FName Chosen);

	/**
	 * Infuse or change a damage type of the weapon at a target, carried or worn: the craft's material leaves the bag,
	 * the type the plan drew or chose is added to the weapon's list or put in the chosen type's place, the weapon
	 * carries the craft's residue, and the days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult CraftDamageType(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
												 ECataclysmForgeDamageTypeCraft Craft, FName Chosen,
												 bool bConfirmedPastThreshold, FCataclysmForgeDamageTypeWork& OutDone,
												 FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	/** What imprinting an enchantment on the item at a target, giving up its affix `Affix`, would take now. Issue #47. */
	static FCataclysmForgeEnchantmentWork ImprintPlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
														 int32 Affix);

	/** What changing the negative of the enchantment pair `Pair` of the item at a target would take now. Issue #47. */
	static FCataclysmForgeEnchantmentWork ChangeNegativePlanFor(const UObject* WorldContext,
																const FCataclysmForgeTarget& Target, int32 Pair);

	/**
	 * Imprint an enchantment on the item at a target, carried or worn, giving up its affix `Affix`: one Aetherial Shard
	 * leaves the bag, the pair `UCataclysmForge::DrawImprint` draws for the run's day is added and the affix removed
	 * (`UCataclysmForge::ApplyImprint`), the item carries 5 more residue, and the days pass. Asks about the threshold as
	 * `SocketGem` does. `OutDone.Drawn` is the pair drawn.
	 */
	static ECataclysmForgeResult Imprint(const UObject* WorldContext, const FCataclysmForgeTarget& Target, int32 Affix,
										 bool bConfirmedPastThreshold, FCataclysmForgeEnchantmentWork& OutDone,
										 FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	/**
	 * Change the negative of the item's enchantment pair `Pair`: one Regret Essence leaves the bag, the pair takes the
	 * negative `UCataclysmForge::DrawChangedNegative` draws for the run's day, a different one with a new roll, the item
	 * carries 100 more residue, and the days pass. Asks about the threshold as `SocketGem` does. `OutDone.Drawn` is the
	 * pair after.
	 */
	static ECataclysmForgeResult ChangeNegative(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
												int32 Pair, bool bConfirmedPastThreshold,
												FCataclysmForgeEnchantmentWork& OutDone, FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options = FCataclysmForgeCraftOptions());

	//~ The residue tools. Issue #47, slice F5.

	/**
	 * What the options do to a craft whose plan takes an item from `ResidueBefore` to `ResidueAfter` in `Days` for
	 * `CraftGold`, against the Chaos Stabilizers, the Tainted Shards and the gold the character playing holds now:
	 * `UCataclysmForge::OptionsOn`.
	 */
	static FCataclysmForgeOptionEffect OptionsFor(const UObject* WorldContext, float ResidueBefore, float ResidueAfter,
												  int32 Days, int64 CraftGold, const FCataclysmForgeCraftOptions& Options,
												  const FCataclysmForgePricing& Pricing = FCataclysmForgePricing(),
												  const FCataclysmForgeCosts& Costs = FCataclysmForgeCosts());

	/**
	 * The empire tree's crafting nodes for a craft on the item at a target whose own material is `CraftMaterial`
	 * (#2741's crafting group, slices C1 and C2): the residue factor that applies to it (Tool Maintenance's always,
	 * Limited Knowledge's below +6, Heat Shielding's on a tier 3+ material), Stable Compounds' share, Master's Touch's
	 * draw for this item today, and the day nodes. Every `OptionsFor` call and the cleanse's plan pass it (G2). None with
	 * no item or no run.
	 */
	static FCataclysmForgeCosts CostsFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
										 FName CraftMaterial);

	/**
	 * The empire tree's forge gold pricing for a craft on the item at a target (#47 slice F6a): each material tier's
	 * factor by `UCataclysmEmpireTree::CraftGoldFactorFor`, from the character's tree, the gold it has spent at the forge
	 * (Treasury Synergy's count) and Forge Upgrades' roll for this item on this day of the run. Every plan call with an
	 * item passes it, and so does every `OptionsFor` call, so a craft and its options take one roll (ruling B).
	 */
	static FCataclysmForgePricing PricingFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target);

	/**
	 * The same for a combine of the rung `Rung` (a stone combine's `Level - 1`, a gem combine's rarity), which has no
	 * item: Forge Upgrades' roll is seeded by the rung and the day (Q10).
	 */
	static FCataclysmForgePricing CombinePricingFor(const UObject* WorldContext, int32 Rung);

	/**
	 * The empire tree's three material draws for a craft on the item at a target, today (#47 slice F6b, Q13): what
	 * `DoItemCraft` keeps of the craft's materials once it can no longer fail (N1). None with no item or no run.
	 */
	static FCataclysmForgeKeeping KeepingFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target);

	/**
	 * The forge screen's words for what the tree will keep of a craft whose own material is `Count` of `Material` and
	 * whose options take `OptionMaterials` (N4), as `UCataclysmForge::KeptText`: "" when nothing is kept.
	 */
	static FString KeptTextFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target, FName Material,
							   int32 Count, const TArray<FCataclysmForgeMaterialTake>& OptionMaterials);

	/** The empire tree's roll nodes for the character playing (#2741's crafting slice C3): Master's Workshop's floor and
	 *  Precision Crafting's chance. The reroll and the Deterministic Affix pass them; none with no character. */
	static FCataclysmForgeRolls RollsFor(const UObject* WorldContext);

	/** The empire tree's three affix nodes, and the rolls Rough Draft's affix is drawn with (slices C3 and C4), for the
	 *  character playing. `DoItemCraft` applies them; none with no character. */
	static FCataclysmForgeAffixNodes AffixNodesFor(const UObject* WorldContext);

	/** The forge screen's words for them at the end of a priced question: `UCataclysmForge::RollsText` when the craft
	 *  draws a roll, then `UCataclysmForge::AffixNodesChanceText` (G3). "" with none held. */
	static FString AffixNodesTextFor(const UObject* WorldContext, bool bDrawsARoll);

	/** What cleansing the item at a target would take now. */
	static FCataclysmForgeCleanse CleansePlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target);

	/**
	 * Halve the residue of the item at a target, carried or worn: one Purified Essence leaves the bag and the days pass.
	 * It lowers residue, so it never asks the threshold question, and it lifts no Consumption mark (Q26).
	 */
	static ECataclysmForgeResult Cleanse(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
										 FCataclysmForgeCleanse& OutDone, FString& OutMessage);

	/** A crafting material's tier, 1 to 5, from `game/Data/CraftingMaterials.csv`; 0 for a row the table lacks (Q17). */
	static int32 MaterialTierOf(FName Material);

	/**
	 * THE ONE SEQUENCE EVERY ITEM CRAFT GOES THROUGH. Issue #47, slice F5, ruled by the coordinating session on
	 * 2026-11-22. In order: the run (`Step.NoRunRefusal`) and the item at the target (`Step.NothingThereRefusal`); for a
	 * worn piece, the Consumption Threshold question when the craft would cross it ("There is no path across the threshold
	 * that does not show the warning first."); the mark set before the change; the gold, once; the materials, through
	 * `TakeMaterials`; the hooks' take; the item changed by `ChangeTheItem` from a copy of it as it stood and the run before
	 * the days pass, which may refuse; the residue written from `Step.ResidueAfter`; the item put back; the hooks' after;
	 * the days, their reports kept; the build the double wears, captured after the change; the craft's line through
	 * `SayTheLine`, then the options' line, then what the days did ("at the forge, days 97 to 99: dungeons resolved:
	 * ...", `FCataclysmPlayLoopLog::AwayAccount`) when a day passed, also kept for the screen
	 * (`UCataclysmGameInstance::KeepCapitalDays`, issue #2857); and the save triggers. ANYTHING THAT CANNOT BE FINISHED
	 * PUTS BACK WHAT IT TOOK.
	 */
	static ECataclysmForgeResult DoItemCraft(
		const UObject* WorldContext, const FCataclysmForgeCraftStep& Step, bool bConfirmedPastThreshold,
		TFunctionRef<bool(const FCataclysmItem& Before, FCataclysmItem& Changed, const UCataclysmEmpireRun& Run,
						  FString& OutRefusal)> ChangeTheItem,
		TFunctionRef<void(const FString& ItemName, int32 DayFrom, int32 DayTo)> SayTheLine,
		const FCataclysmForgeCraftHooks& Hooks, FString& OutMessage);

	//~ Dismantling a carried piece. Issue #2848, slice S2.

	/**
	 * What dismantling the item at a target would give now, before the draw: `UCataclysmForge::DismantleOf` with the
	 * empire tree's Salvage Protocol and Disassembly Expert. REFUSED for a worn piece: only a carried piece is dismantled
	 * ("Take the piece off before dismantling it.").
	 */
	static FCataclysmForgeDismantle DismantlePlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target);

	/**
	 * Dismantle the carried piece at a target, through `DoItemCraft`, instantly (D2): no gold, no days, no residue, no
	 * material. Its yield is drawn from the item as it stood and the run's day, on `AffixCraftSeed` with
	 * `UCataclysmForge::DismantleSalt` (D5), with the player's own magic find (D3); then the piece leaves the bag and the
	 * fragments, the Dust, the upgrade stones and the gems join it. Nothing it gives back is refused for room: the
	 * materials and gems go to stores that never refuse (#2849; until then a full bag refused first, D4). `OutDone`
	 * holds what was drawn.
	 */
	static ECataclysmForgeResult Dismantle(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
										   FCataclysmForgeDismantle& OutDone, FString& OutMessage);

private:
	/**
	 * THE ONE PLACE A CRAFT'S MATERIALS ARE TAKEN, material by material, recording what was taken so a failure gives back
	 * exactly that. Each material's tier is read from the table (`MaterialTierOf`). Slice F5 takes everything; the gold
	 * session's F6b decides here whether Material Conservation, Resource Management or Tempting Fate keeps one (Q17).
	 */
	static bool TakeMaterials(UCataclysmInventoryComponent* Inventory, const TArray<FCataclysmForgeMaterialTake>& Materials,
							  TArray<FCataclysmForgeMaterialTake>& OutTaken);

	/** Puts back what `TakeMaterials` took. */
	static void GiveMaterialsBack(UCataclysmInventoryComponent* Inventory, const TArray<FCataclysmForgeMaterialTake>& Taken);

	/**
	 * The part `Imprint` and `ChangeNegative` share, given a possible plan, in the order slice F1's `CraftAffix` keeps:
	 * the threshold question, the gold, the material, the draw and the item changed, the days, the line and the save.
	 * Anything that cannot be finished, a draw that fails included, puts back what it took.
	 */
	static ECataclysmForgeResult DoEnchantmentWork(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
												   const FCataclysmForgeEnchantmentWork& Plan,
												   bool bConfirmedPastThreshold, FCataclysmForgeEnchantmentWork& OutDone,
												   FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options);

	/**
	 * The part `AddAffix` and `ShiftAffix` share, given a possible plan drawn for the run's day: the threshold question,
	 * the gold, the material, the item changed, the days, the line and the save, in `CraftAffix`'s order. Anything that
	 * cannot be finished puts back what it took.
	 */
	static ECataclysmForgeResult DoAffixChange(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											   const FCataclysmForgeAffixChange& Plan, bool bConfirmedPastThreshold,
											   FCataclysmForgeAffixChange& OutDone, FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options);

	/**
	 * The part `AddSocket` and `ChangeSocketType` share, given a possible plan, as `DoSocketing` is for socketing: the
	 * threshold question, the gold, the material, the item changed, the days, the line and the save. Anything that cannot
	 * be finished puts back what it took.
	 */
	static ECataclysmForgeResult DoSocketWork(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											  const FCataclysmForgeSocketWork& Plan, bool bConfirmedPastThreshold,
											  FCataclysmForgeSocketWork& OutDone, FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options);

	/**
	 * The part `SocketGem` and `UnsocketGem` share, given a possible plan: the threshold question, taking the material
	 * (and for a socketing the gem), changing the item, giving the gem back (for an unsocketing), the days, the line and
	 * the save. Anything that cannot be finished puts back what it took, so nothing is half done.
	 */
	static ECataclysmForgeResult DoSocketing(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											 const FCataclysmForgeSocketing& Plan, bool bConfirmedPastThreshold,
											 FCataclysmForgeSocketing& OutDone, FString& OutMessage,
		const FCataclysmForgeCraftOptions& Options);
};
