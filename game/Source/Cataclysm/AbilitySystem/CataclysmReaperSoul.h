// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmReaperSoul.generated.h"

class UWorld;

/**
 * A Soul, left where a minion or thrall died: Reaper's Embrace's six-piece bonus, "When a minion or pet dies, it leaves
 * behind a "Soul" that can be picked up. Picking up a Soul grants a stack of Soul Strike that increases your damage by
 * 10% for 5 seconds. Max 10 stacks." The owner's ruling #8 of 2026-10-17 (picked up by walking within about 1.5 m; it
 * lasts the floor) and the coordinating session's rulings R1-R8 of 2026-10-18; `docs/DECISIONS.md` carries both.
 *
 * LEFT BY THE COMMANDER'S DEATH HOOK, `UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero`, for exactly the deaths
 * that hook answers (R1): a minion's or thrall's health reaching nought by a blow, a tick or a direct write, a gadget
 * or deployable killed, a minion Sacrificial Ward spends, and a Tyrant's Minion's and a Warhound's own. A lifetime
 * running out writes no health and reaches none of it, so it leaves no Soul; nor does a minion the summon cap
 * destroys. Only where the commander holds `MinionDeathLeavesSoulStat` above nought.
 *
 * TAKEN ONLY BY THE COMMANDER WHOSE MINION LEFT IT (R2), while alive, by walking within `PickUpReachCm` of it along the
 * floor (R3), looked for every frame by the player controller's pass beside the material pick-up (R4). Taking it
 * raises `soul_picked_up` on the taker's ability system and destroys it; Soul Strike is rows on that event (R5, R6). A
 * Soul whose commander no longer wears the six-piece is still taken: the event is raised and no row answers it.
 *
 * IT LASTS THE FLOOR (#8, R7). No lifespan: it ends only when it is taken or when the floor is cleared,
 * `UCataclysmFloorContents::ClearTheFloor` destroying every Soul as it destroys every drop. A Soul whose commander has
 * left stays until then, taken by nobody.
 *
 * WHAT THE PLAYER SEES (R8): a name tag, "Soul", in one fixed colour, drawn by `ACataclysmHUD` over where it lies as a
 * floor object's tag is drawn. No mesh. NOT CLICKED: a click on its tag finds nothing, since it is taken by walking.
 *
 * NOT SAVED. A floor restored from a save is cleared first, which destroys every Soul on it; none is written back.
 */
UCLASS()
class CATACLYSM_API ACataclysmReaperSoul : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmReaperSoul();

	/**
	 * The flag a minion's death leaves a Soul by, read off the COMMANDER with no tags when a minion or thrall of its
	 * dies. A flat 1 on the six-piece's row; nought, or no row, leaves none. No gameplay attribute and no base: it is in
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()`.
	 */
	static const TCHAR* MinionDeathLeavesSoulStat;

	/** How far a Soul is reached from, along the floor, in centimetres: the owner's "about 1.5 m" (#8, R3). */
	static constexpr float PickUpReachCm = 150.0f;

	/** The words on its tag (R8). */
	static const TCHAR* NameText;

	/** The one colour its tag is drawn in (R8): a pale blue, which no rarity of gear or of enemy uses. */
	static FLinearColor NameColour();

	/** How far above where it lies its tag is drawn, as a drop's is. */
	static constexpr float NameHeightCm = 60.0f;

	/** The commander whose minion or thrall left it: the only one who may take it (R2). */
	TWeakObjectPtr<AActor> LeftByCommander;

	/**
	 * Leave a Soul for `Commander` at `Where`, where a minion or thrall of its has just died. Called by the commander's
	 * death hook, so for exactly the deaths R1 names. Nothing when `Commander` is gone or holds
	 * `MinionDeathLeavesSoulStat` at nought. A DEAD COMMANDER'S MINION STILL LEAVES ONE: the Soul lasts the floor, and a
	 * player stands up again on it.
	 *
	 * @return the Soul left, or null
	 */
	static ACataclysmReaperSoul* LeaveFor(AActor* Commander, const FVector& Where);

	/**
	 * Take every Soul `Taker` may take: those it is the commander of, lying within `PickUpReachCm` of it along the
	 * floor. Nothing while `Taker` is dead. Each Soul taken raises `soul_picked_up` once on the taker's ability system,
	 * then is destroyed. Called every frame by `ACataclysmPlayerController` with its pawn (R4), and by tests.
	 *
	 * @return how many were taken
	 */
	static int32 TakeSoulsNear(AActor* Taker);

	/**
	 * Every Soul within `Range` of `Standing`, nearest first: the Souls whose tags the HUD draws. Asked every frame
	 * rather than kept, for the reason `UCataclysmDropPickup::DropsToName` gives.
	 */
	static void SoulsToName(const UWorld* World, const FVector& Standing, float Range,
							TArray<ACataclysmReaperSoul*>& Out);
};
