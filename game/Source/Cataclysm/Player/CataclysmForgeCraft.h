// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "CataclysmForgeCraft.generated.h"

class ACataclysmPlayerCharacter;

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
 * or a resolve inside them happens on its own day. A PLAY LOOP line says it, and `ForgeCrafted` saves the character
 * record and the run record.
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
										 FString& OutMessage);

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
										   FCataclysmForgeSocketing& OutDone, FString& OutMessage);

	/**
	 * Take the gem out of a socket of the item at a target: one Dismantling Dust leaves the bag, the gem goes back to
	 * it, the item carries 3 more residue, and the days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult UnsocketGem(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											 int32 Socket, bool bConfirmedPastThreshold,
											 FCataclysmForgeSocketing& OutDone, FString& OutMessage);

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
										   FString& OutMessage);

	/**
	 * Make an empty socket of the item at a target a socket of `NewType`: one Focusing Lens leaves the bag, the item
	 * carries 10 more residue, and the days pass. Asks about the threshold as `SocketGem` does.
	 */
	static ECataclysmForgeResult ChangeSocketType(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
												  int32 Socket, ECataclysmSocketType NewType,
												  bool bConfirmedPastThreshold, FCataclysmForgeSocketWork& OutDone,
												  FString& OutMessage);

private:
	/**
	 * The part `AddSocket` and `ChangeSocketType` share, given a possible plan, as `DoSocketing` is for socketing: the
	 * threshold question, the gold, the material, the item changed, the days, the line and the save. Anything that cannot
	 * be finished puts back what it took.
	 */
	static ECataclysmForgeResult DoSocketWork(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											  const FCataclysmForgeSocketWork& Plan, bool bConfirmedPastThreshold,
											  FCataclysmForgeSocketWork& OutDone, FString& OutMessage);

	/**
	 * The part `SocketGem` and `UnsocketGem` share, given a possible plan: the threshold question, taking the material
	 * (and for a socketing the gem), changing the item, giving the gem back (for an unsocketing), the days, the line and
	 * the save. Anything that cannot be finished puts back what it took, so nothing is half done.
	 */
	static ECataclysmForgeResult DoSocketing(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
											 const FCataclysmForgeSocketing& Plan, bool bConfirmedPastThreshold,
											 FCataclysmForgeSocketing& OutDone, FString& OutMessage);
};
