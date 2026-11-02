// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCorruptedStalkerCharacter.h"
#include "Character/CataclysmPlayerBuild.h"
#include "CataclysmConsumptionDoubleCharacter.generated.h"

/**
 * The corrupted double: the creature that hunts a character marked by crossing the Consumption Threshold, wearing that
 * character's own build. Issue #2688, slice 2a, the build. `docs/Cataclysm_GDD_v2.md`, "What crossing it does".
 *
 * A CORRUPTED STALKER IN EVERY WAY BUT THREE, because it is the same thing -- a creature of the monsters' side
 * carrying a player's build -- and the Stalker's brain, potions, Fervour and Boss's pay already work for one:
 *
 * - THE PLAYER'S OWN NUMBERS, NOT AN ENTRY'S AT A TIER (R3, ruled 2026-11-02): the items as they were worn, with
 *   their rolled values and gear levels, and the character's own level, class, attribute points, passives and skill
 *   choices. Nothing is drawn again, so `PiecesAt` is never called.
 * - NO PER-TIER MULTIPLIERS (R4, ruled 2026-11-02): `CorruptedStalkerHealthMultiplier` and
 *   `CorruptedStalkerDamageMultiplier` are the Stalker's alone.
 * - NONE OF ITS GEAR DROPS (O2, the owner's decision of 2026-11-02): it wears copies of the player's items, so a drop
 *   would duplicate them. It pays the Boss rung's experience and materials as the Stalker does.
 *
 * WHAT SLICE 2A DOES NOT DO: nothing raises it, nothing saves it, and its death clears nothing. The arrival on the next
 * floor, the capture at the crossing (the owner's decision of 2026-11-02: the double is the character as it was at the
 * crossing), the save and what its death does are slices 2b and 2c.
 */
UCLASS()
class CATACLYSM_API ACataclysmConsumptionDoubleCharacter : public ACataclysmCorruptedStalkerCharacter
{
	GENERATED_BODY()

public:
	/**
	 * Wears a player's build: its items on an equipment component that belongs to no actor (the Stalker's reason, in
	 * its header), gathered and applied by `UCataclysmEquipmentComponent::RefreshAttributesAs`, the one place that
	 * gathers every stat source (issue #1054), with the pools filled; then the Stalker's starting Fervour, its skills
	 * from every combination of its worn weapon types and the damage types they carry with the player's choices
	 * (`UCataclysmWeaponSkills::FillWithChoices`), its first weapon's basic attack, and `bHoldsAPlayerBuild`.
	 *
	 * @return false when the creature has no ability system or nothing could be written; nothing is set then
	 */
	bool WearThePlayersBuild(const FCataclysmPlayerBuild& Build);

	/** The build it wears, or an empty one before `WearThePlayersBuild`. */
	const FCataclysmPlayerBuild& WornBuild() const { return HeldBuild; }

	/** Nothing: none of its gear drops (O2). Its materials and experience come from the Boss rung it pays as. */
	virtual void DropWhatItWore(UWorld* World, const FVector& At, FRandomStream& Stream) override;

private:
	UPROPERTY()
	FCataclysmPlayerBuild HeldBuild;
};
