// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CataclysmBuriedWeapon.generated.h"

/**
 * A weapon left in a creature, which tears free when it dies and buries itself
 * in the next one.
 *
 * WHAT ASKS FOR IT. The Axe's Harrower: "bury a burning axe in an enemy up to 10
 * meters away, setting them alight. The axe stays where it lands. When that
 * enemy dies it tears free and buries itself in the nearest living enemy within
 * 10 meters, and it goes on doing so until nothing is left in reach." Its row
 * writes `OnDeath=Leap` and `OnDeathRange=10`, and nothing read either.
 *
 * A SEPARATE COMPONENT FROM `UCataclysmCurseSpread`, WHICH IT SITS BESIDE.
 * Both are set off by the same death and both reach for the nearest enemy, and
 * there the resemblance stops: that one copies whatever tags its host happens to
 * carry and applies no damage, while this one carries a damage figure, the
 * firing skill's own tags and its identity, and deals a blow. Folding them into
 * one component with a mode would put two payloads on one object and make every
 * reader check which was in use.
 *
 * IT MOVES ITSELF ON RATHER THAN BEING RESPAWNED. When the axe leaps, it marks
 * its new host with the same numbers, which is what "goes on doing so until
 * nothing is left in reach" means. It stops when nothing is in range, and there
 * is nothing else to stop it -- the row states no limit and neither does this.
 *
 * ONE PER ACTOR, REFRESHED RATHER THAN ADDED TO, the same single-stack rule
 * every player-applied effect in this design follows.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmBuriedWeapon : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Leave a weapon in this creature, or refresh the one already in it.
	 *
	 * @param Target          the creature it is buried in
	 * @param Caster          who threw it, credited with what it deals
	 * @param RangeCm         how far it reaches for its next host
	 * @param InDamagePercent what each blow deals, as percent of weapon damage
	 * @param InSkillTags     the firing skill's tags, so its blows scale and are
	 *                        mitigated the way that skill's blows should be
	 * @param bInBurns        whether it sets what it strikes alight
	 * @return the component, or null if the creature cannot take one
	 */
	static UCataclysmBuriedWeapon* BuryIn(AActor* Target, AActor* Caster,
										  float RangeCm, float InDamagePercent,
										  const FGameplayTagContainer& InSkillTags,
										  bool bInBurns);

	/**
	 * Tear free of a dying creature and bury in the nearest living enemy.
	 *
	 * CALLED FROM `UCataclysmSkillEffects::MarkDead`, the one place a death is
	 * recorded for the player and every creature, so an axe comes free whether
	 * its host was killed by a blow, by a burn or by a patch of burning ground.
	 *
	 * IT RUNS BEFORE THE DEATH IS RECORDED, so the dying creature is still an
	 * ordinary target to the search and is excluded by name instead.
	 *
	 * @return whether it found a new host
	 */
	static bool LeapFromDying(AActor* Dying);

	/** How far it reaches for its next host, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	float RangeCm = 0.0f;

	/** What each blow deals, as a percent of the caster's weapon damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	float DamagePercent = 0.0f;

	/** True when it sets what it strikes alight. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	bool bBurns = false;

	/** The firing skill's own tags, carried so its blows are scaled as its own. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	FGameplayTagContainer SkillTags;

	/** Who threw it. Credited with everything it deals afterwards. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	TWeakObjectPtr<AActor> Caster;

	/** How many creatures it has moved through. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	int32 Leaps = 0;
};
