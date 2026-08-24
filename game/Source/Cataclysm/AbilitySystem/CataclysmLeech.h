// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmLeech.generated.h"

class AActor;
class UAbilitySystemComponent;

/** Which of the three pools a leech payment fills. */
UENUM()
enum class ECataclysmLeechPool : uint8
{
	Health,
	Mana,
	EnergyShield,
};

/**
 * One hit's worth of leech, still being paid out.
 *
 * ONE OF THESE PER HIT, NOT ONE PER CHARACTER, because the design says so:
 * "Leech from several hits runs at the same time. Each hit starts its own
 * 3-second payout. A character hitting continuously therefore reaches a steady
 * state of roughly three hits' worth of leech in flight."
 */
USTRUCT()
struct CATACLYSM_API FCataclysmLeechPayment
{
	GENERATED_BODY()

	/** Which pool it fills. */
	UPROPERTY()
	ECataclysmLeechPool Pool = ECataclysmLeechPool::Health;

	/** How much is still owed. */
	UPROPERTY()
	float Remaining = 0.0f;

	/** How many seconds are left to pay it over. */
	UPROPERTY()
	float SecondsLeft = 0.0f;
};

/**
 * Leech: what a hit gives back to whoever landed it.
 *
 * THE THREE LEECH AFFIXES DID NOTHING AT ALL UNTIL ISSUE #895. The `LifeLeech`,
 * `ManaLeech` and `EnergyShieldLeech` attributes existed, were clamped and were
 * replicated, and no code in the project read any of them. `Stat_Flat_life_leech`
 * reached its attribute and the other two were dropped before they got that far,
 * and all three were worth nothing either way.
 *
 * THE DESIGN STATES EVERY RULE, at docs/Cataclysm_GDD_v2.md, Leech:
 *
 *   **A percentage of the damage actually dealt.** "A character with 3% life
 *   leech who lands a hit for 400 damage leeches 12 health. It is the damage the
 *   target really took, after its resistances, armour and block, not the damage
 *   the attack would have done to nothing."
 *
 *   **Overkill does not count.** "An enemy with 25 health left, hit for 400,
 *   contributes 25 to the leech calculation and not 400." That cap is already in
 *   the hit result: UCataclysmDamageCalculation::Resolve writes DealtToHealth as
 *   the smaller of the damage and the target's remaining health, and caps what a
 *   shield absorbed at the shield. So the three figures it reports are already
 *   what this needs, and nothing here has to cap anything again.
 *
 *   **Paid out over 3 seconds rather than at once.** "Instant leech makes a
 *   character that is winning unkillable and does nothing for one that is
 *   losing, because the recovery arrives only as fast as the damage does."
 *
 * WHERE THE SHAPE COMES FROM. The design records it: Last Epoch pays leech over a
 * fixed 3-second period and excludes overkill, and this is that model. Path of
 * Exile instead caps each instance at 10% of maximum life and the total rate at
 * 20% per second; the simpler of the two was taken deliberately.
 *
 * PAID ON THE SAME TIMER AS REGENERATION, every quarter second, from
 * ACataclysmCharacterBase. A pool coming back is not an event a player should
 * see land, and a bar that jumps once a second reads as broken.
 */
UCLASS()
class CATACLYSM_API UCataclysmLeech : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Seconds a hit's leech takes to arrive in full.
	 *
	 * FROM THE DESIGN, WHICH ALSO SAYS IT IS EXPECTED TO MOVE: "Both numbers are
	 * a starting point and expected to move. The 3-second period and the affix
	 * values are tuned against real play."
	 */
	static constexpr float PayoutSeconds = 3.0f;

	/**
	 * What a hit leeches, given the damage the target really took.
	 *
	 * @param DamageTaken   after mitigation and already capped at what the
	 *                      target had, which is what the hit result reports
	 * @param LeechPercent  the attacker's figure for one of the three pools
	 */
	static float AmountFrom(float DamageTaken, float LeechPercent);

	/**
	 * How much of one payment arrives in a step of this length.
	 *
	 * LINEAR, AND THE LAST STEP PAYS WHATEVER IS LEFT. Paying a fixed fraction
	 * per step would leave a shrinking remainder that never reaches zero, so a
	 * step at least as long as the time left pays the whole balance.
	 */
	static float PaidInStep(const FCataclysmLeechPayment& Payment,
							float SecondsInStep);

	/**
	 * Start a payout on the attacker for a hit that took this much.
	 *
	 * NOTHING FOR A HIT THAT TOOK NOTHING, so an evaded hit, a hit a shield and
	 * armour stopped completely, and a hit on a target already at zero all leech
	 * nothing.
	 *
	 * ONE PAYMENT PER POOL THE ATTACKER LEECHES INTO, and none for a pool whose
	 * leech is zero, which is every pool for almost every character.
	 */
	static void NoteHit(UAbilitySystemComponent* Attacker, float DamageTaken);

	/**
	 * Pay one step of every outstanding payment into its pool.
	 *
	 * NOTHING FOR THE DEAD, and nothing when a pool is already full, both for
	 * the same reasons UCataclysmRegeneration::ApplyStep gives.
	 */
	static void PayOutStep(AActor* Character, float SecondsInStep);
};
