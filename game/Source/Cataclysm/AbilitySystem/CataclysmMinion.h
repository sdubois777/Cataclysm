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

	/**
	 * How far it can reach, in centimetres, when its type states nothing.
	 *
	 * A DEFAULT NOW, NOT THE ANSWER. Every minion type in
	 * `game/Data/MinionTypes.csv` states its own reach -- 2 m for an imp, 10 m
	 * for a bolt turret, 15 m for a ballista -- and `Spawn` applies it. This is
	 * what a minion spawned without naming a type gets. Issue #340 asked for
	 * this and issue #622 is what needed it.
	 */
	static constexpr float DefaultReachCm = 300.0f;

	/**
	 * How far it notices something to chase, in centimetres, when its type
	 * states nothing.
	 *
	 * A DEFAULT NOW, NOT THE ANSWER, for the same reason as the reach above:
	 * every minion type states its own, from 2 m for a spike trap to 15 m for a
	 * ballista. Fifteen metres remains the fallback because that is what issue
	 * #163 settled for an imp summoned across a room.
	 */
	static constexpr float DefaultNoticeRadiusCm = 1500.0f;

	/**
	 * Seconds between its attacks, when its type states nothing.
	 *
	 * THE COMMENT HERE USED TO CITE A RULE THE DESIGN HAS REVERSED. It said the
	 * design "states one attack per second for every minion in the game". Issue
	 * #209 reversed that: `docs/Cataclysm_GDD_v2.md` now states "Every minion
	 * type has its own stats. A minion is not a percentage of its summoner." The
	 * table gives five different intervals, from 0.8 s for a mote to 3.0 s for a
	 * spike trap, and `Spawn` applies them.
	 */
	static constexpr float DefaultAttackIntervalSeconds = 1.0f;

	/**
	 * Percent of the summoner's weapon damage one of its hits deals.
	 *
	 * FROM THE DESIGN, AND IT WAS NOT. This was 25 and was labelled a judgement,
	 * because nothing in the design said what a summoned imp hit for. Issue #165
	 * asked for a real figure and the design now states 30, taken from Diablo IV,
	 * whose Necromancer minions gain 30% of the player's weapon damage.
	 *
	 * STILL A SINGLE RULE, AND THE DESIGN NO LONGER AGREES WITH IT. Issue #209
	 * replaced this with each type's own base damage plus an amount per level,
	 * scaled by an attribute through `game/Data/MinionScaling.csv`. That model is
	 * NOT implemented here and this constant is what still runs. Two things it
	 * needs do not exist: a way to read the summoner's level, and any code at all
	 * that applies the minion scaling table. **Issue #340 tracks the remaining
	 * gap**, and `test_the_code_is_recorded_as_behind_the_design` in
	 * `tools/tests/test_minion_damage.py` fails if that stops being true.
	 */
	static constexpr float DamagePercentOfSummoner = 30.0f;

	/**
	 * Put one in the world.
	 *
	 * @param Summoner       whose skill made it; its damage and its side are theirs
	 * @param Location       where it appears
	 * @param Lifetime       seconds before it goes away on its own
	 * @param bBurns         whether what it hits is set alight
	 * @param TypeName       a row of game/Data/MinionTypes.csv, such as "Ballista".
	 *                       Empty spawns one carrying the defaults above, which is
	 *                       what every caller did before issue #622
	 * @param HealthPercent  percent of its type's own health to give it, or 0 for
	 *                       that type's health unchanged. Iron Fortress states 150
	 */
	static ACataclysmMinion* Spawn(AActor* Summoner, const FVector& Location,
								   float Lifetime, bool bBurns,
								   const FString& TypeName = FString(),
								   float HealthPercent = 0.0f);

	/**
	 * The imported minion type table, or null with the reason logged.
	 *
	 * Loads the DataTable ASSET rather than the CSV, which is what makes it work
	 * in a packaged build: game/Data/ is reviewable output and is not cooked.
	 */
	static const UDataTable* LoadTypeTable();

	/** The row for a type name, or null when the table has no such row. */
	static const struct FCataclysmMinionTypeRow* FindType(const UDataTable* Table,
														  const FString& TypeName);

	/** Which row of the minion type table this one was made from. Empty if none. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	FString TypeName;

	/**
	 * Percent of its type's own health the skill that deployed it asked for, or
	 * zero for that type's health unchanged.
	 *
	 * RECORDED AND NOT YET APPLIED, WHICH IS SAID HERE RATHER THAN HIDDEN.
	 * Health cannot be set from the type until the summoner's level can be read,
	 * because the table states BaseHealth and HealthPerLevel rather than one
	 * number. Issue #340 holds that. Iron Fortress is the only skill that states
	 * this, at 150.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float DeployedHealthPercent = 0.0f;

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

	/**
	 * This one's own numbers, taken from its type's row when it was given one.
	 *
	 * PER INSTANCE RATHER THAN PER CLASS, WHICH IS THE WHOLE POINT. A ballista
	 * and a spike trap are both this class and must not share a reach or a
	 * firing rate. They start at the defaults above so a minion spawned without
	 * a type behaves exactly as every minion did before issue #622.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float ReachCm = DefaultReachCm;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float NoticeRadiusCm = DefaultNoticeRadiusCm;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float AttackIntervalSeconds = DefaultAttackIntervalSeconds;

	/**
	 * Whether it goes to its target or stays where it was put.
	 *
	 * READ OFF THE TYPE'S MOVE SPEED RATHER THAN SET BY THE SKILL. A bolt
	 * turret, a ballista and a spike trap all state a move speed of zero in
	 * `game/Data/MinionTypes.csv`, which is what makes a deployable a thing that
	 * stays put; an imp states 4.4 and a mote 5.5. So the difference between the
	 * Summon shape and the Deployable shape is where the thing is placed, and
	 * whether it then walks is a property of what was placed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	bool bStaysWhereItIsPut = false;

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
