// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "CataclysmMinion.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;

/**
 * A summoned imp: it hits the nearest enemy in reach until its time runs out.
 *
 * WHAT IT DOES NOT DO, AND SAYING SO PLAINLY. It does not move. This project has
 * no artificial intelligence of any kind -- no behaviour tree, no navigation
 * mesh built, no controller for anything but the player -- so "it attacks the
 * nearest enemy" is implemented as "it hits the nearest enemy within its reach",
 * and an enemy outside that reach is simply not attacked. Summon Imp's written
 * behaviour is "attacks the nearest enemy, sets what it hits alight, and lasts
 * 20 seconds"; the second and third of those are real and the first is real only
 * within reach. Issue #163.
 *
 * It carries an ability system component so that it is a thing the world can
 * damage and kill, rather than an invulnerable timer. It is deliberately not a
 * character: without navigation there is nothing for a character movement
 * component to do, and a character costs considerably more.
 */
UCLASS()
class CATACLYSM_API ACataclysmMinion : public AActor, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACataclysmMinion();

	/** How far it can reach, in centimetres. Its own, not the summoning skill's. */
	static constexpr float ReachCm = 300.0f;

	/** Seconds between its attacks. */
	static constexpr float AttackIntervalSeconds = 1.0f;

	/**
	 * Percent of the summoner's weapon damage one of its hits deals.
	 *
	 * A JUDGEMENT, NOT A DESIGN FIGURE. Nothing in the design states what a
	 * summoned imp hits for. Three of them at 25% each, attacking once a second,
	 * come to 75% of weapon damage per second, which sits below an automatic
	 * basic attack at 128% to 150% per second -- so a Ritualist holding three
	 * imps has added meaningfully to their damage without the minions becoming
	 * the whole of it. Issue #165 asks the design to state a real figure.
	 */
	static constexpr float DamagePercentOfSummoner = 25.0f;

	/**
	 * Put one in the world.
	 *
	 * @param Summoner  whose skill made it; its damage is a share of theirs
	 * @param Location  where it appears
	 * @param Lifetime  seconds before it goes away on its own
	 * @param bBurns    whether what it hits is set alight
	 */
	static ACataclysmMinion* Spawn(AActor* Summoner, const FVector& Location,
								   float Lifetime, bool bBurns);

	/** Whose it is. Its damage is a share of this actor's weapon damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	TObjectPtr<AActor> Summoner;

	/** Whether what it hits is set alight. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	bool bBurnsWhatItHits = false;

	/** How many times it has attacked. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	int32 AttacksMade = 0;

	/**
	 * Blow up, hurting everything within RadiusCm, then be destroyed.
	 *
	 * Summon Imp: "Summoning a fourth destroys the oldest, which explodes for
	 * damage in a 3 meter radius."
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Minion")
	void Explode(float RadiusCm, float DamagePercent);

	/** Hit the nearest enemy in reach. Called on a timer, and by tests. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Minion")
	void AttackOnce();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Minion")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;

private:
	FTimerHandle AttackTimer;
};
