// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "CataclysmMinion.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UStaticMeshComponent;

/**
 * A summoned imp: it chases the nearest enemy, hits it, and expires.
 *
 * A CHARACTER, WHICH IT WAS NOT. It used to be a bare AActor with an ability
 * system component and nothing else, and that had two consequences that were not
 * obvious from reading it. An AActor whose components are all non-scene
 * components gets no root component, and an actor with no root component reports
 * its location as the world origin however it was spawned -- so an imp searched
 * for targets around (0,0,0) rather than around itself. It also had no collision
 * of any kind, so nothing could find it: the class comment claimed it was "a
 * thing the world can damage and kill" and no sphere overlap could return it.
 * Both are fixed by it being a character, which brings a capsule. Issue #163.
 *
 * IT SHARES ITS BRAIN WITH A MONSTER. ACataclysmEnemyController possesses this
 * and ACataclysmEnemyCharacter alike; the only difference is which side it is
 * on and what its attacks are worth. Its side is copied from its summoner, so
 * everything the summoner is hostile to, it is hostile to.
 */
UCLASS()
class CATACLYSM_API ACataclysmMinion : public ACataclysmCharacterBase
{
	GENERATED_BODY()

public:
	ACataclysmMinion();

	/** How far it can reach, in centimetres. Its own, not the summoning skill's. */
	static constexpr float ReachCm = 300.0f;

	/**
	 * How far it notices something to chase, in centimetres.
	 *
	 * A JUDGEMENT, NOT A DESIGN FIGURE. Summon Imp says only "it attacks the
	 * nearest enemy". Fifteen metres is the same distance Subjugate reaches, and
	 * it is far enough that an imp summoned on one side of a room goes to a fight
	 * on the other side rather than standing still, which was the whole of what
	 * issue #163 reported.
	 */
	static constexpr float NoticeRadiusCm = 1500.0f;

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
	 * @param Summoner  whose skill made it; its damage and its side are theirs
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

	/** A stand-in body, so an imp is visible before there is any art. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/**
	 * Blow up, hurting everything within RadiusCm, then be destroyed.
	 *
	 * Summon Imp: "Summoning a fourth destroys the oldest, which explodes for
	 * damage in a 3 meter radius."
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Minion")
	void Explode(float RadiusCm, float DamagePercent);

	/** Hit the nearest enemy in reach. Called by tests. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Minion")
	void AttackOnce();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	//~ Driven by ACataclysmEnemyController
	virtual float AttackReachCm() const override { return ReachCm; }
	virtual float SightRadiusCm() const override { return NoticeRadiusCm; }
	virtual float SecondsBetweenAttacks() const override { return AttackIntervalSeconds; }
	virtual void AttackTarget(AActor* Target) override;
	//~ End

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Minion")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;
};
