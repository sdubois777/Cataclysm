// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CataclysmCoffinNails.generated.h"

/**
 * The nails a skill has driven into a creature, and the critical strike chance they give whoever drove them. Issue
 * #2885, the Death Crossbow draft's C1, slice DS5: `CritChanceAgainst=N; CritChanceAgainstMax=M; EffectDuration=S` on a
 * Projectile.
 *
 * WHAT ASKS FOR IT. The Crossbow's Coffin Nail, approved by the owner on 2026-11-24 with its recommendation that "Coffin
 * Nail's +5 is flat critical strike chance added to the base": "Drive a coffin nail into the first enemy in a line up to
 * 14 meters away. The nail stays in: for every nail an enemy carries, your critical strike chance against it is 5%
 * higher, up to 4 nails, and they stay until 4 seconds pass without a new one." Its shape and numbers are Diablo IV's
 * Heartseeker upgrade, taken whole: "Critical Strike Chance against them is increased by 5% for 4 seconds", up to 20%.
 *
 * PERCENTAGE POINTS ADDED TO THE CHANCE THE BLOW ALREADY HAS, the owner's choice: a blow of the nailer's at a nailed
 * creature has `CritChanceAgainst` for each nail added to whichever chance it would have had -- its skill's stated
 * chance when it states one (Mercy Stroke's 10, so "Mercy Stroke finishes it with the nails' crit chance"), the
 * character's own otherwise -- before the character's critical strike ceiling, which still holds it
 * (`UCataclysmVitalAttributeSet`'s damage branch, the one place a blow's chance is settled). EVERY BLOW OF THE NAILER'S,
 * as the draft says ("the off-hand dagger's, the crows', the basic attack's"), and no one else's.
 *
 * ONE NAILER'S NAILS AT A TIME, A JUDGEMENT OF SLICE DS5, as every player-applied effect in this design is single
 * stack: a second nailer's nail starts the count again for that nailer. The game has one player, so the two readings
 * differ only for a nail some other character drove, which nothing does.
 *
 * NO TIMER. The nails are read lazily: once `HoldSeconds` have passed since the last nail, a creature carries none, and
 * the next nail is the first again. "They stay until 4 seconds pass without a new one" -- each new nail renews them all.
 *
 * A COMPONENT, for `UCataclysmCurseSpread`'s reason: the nails outlive the shot that drove them, and a component is
 * destroyed with its creature and needs no sweeping.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmCoffinNails : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Drive one nail into `Target` for `Driver`, and answer how many of `Driver`'s nails it carries now.
	 *
	 * CALLED FROM `UCataclysmSkillTemplate::LayWhatALandedShotCarries`, at a contact of a shot stating `CritChanceAgainst`
	 * that landed and carried damage. The shot that drives a nail does not benefit from it: its blow was settled first.
	 *
	 * @param PerNail      percentage points of critical strike chance each nail gives; nothing is driven at nought
	 * @param Ceiling      the most the nails together give, which caps how many count; nought or less for no ceiling
	 * @param HeldSeconds  how long the nails stay without a new one; nothing is driven at nought
	 * @return the nails `Target` carries for `Driver` after this one, nought when nothing could be driven
	 */
	static int32 DriveInto(AActor* Target, AActor* Driver, float PerNail, float Ceiling, float HeldSeconds);

	/** How many of `Driver`'s nails `Target` carries now: nought once they have lapsed, or for another's nails. */
	static int32 NailsIn(const AActor* Target, const AActor* Driver);

	/**
	 * The critical strike chance, in percentage points, the nails `Striker` drove into `Target` add to its blows at it
	 * now: each nail's points, held to the ceiling. Nought for a creature carrying none of `Striker`'s, or whose nails
	 * have lapsed. Asked by `UCataclysmVitalAttributeSet` as a blow's chance is settled.
	 */
	static float CritChanceAgainst(const AActor* Target, const AActor* Striker);

	/** Whose nails these are. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Nails")
	TWeakObjectPtr<AActor> Nailer;

	/** How many nails count, held so their points do not pass the ceiling. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Nails")
	int32 Nails = 0;

	/** Percentage points of critical strike chance each nail gives, from the row that drove the last. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Nails")
	float PointsPerNail = 0.0f;

	/** The most the nails together give, in percentage points; nought or less for no ceiling. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Nails")
	float CeilingPoints = 0.0f;

	/** How long the nails stay without a new one, in seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Nails")
	float HoldSeconds = 0.0f;

	/** The world's time when the last nail went in. */
	double LastNailAt = 0.0;

private:
	/** Whether the nails held here have lapsed by `Now`: their hold has passed since the last nail. */
	bool HaveLapsed(double Now) const;
};
