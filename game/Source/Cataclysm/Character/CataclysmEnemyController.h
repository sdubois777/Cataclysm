// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CataclysmEnemyController.generated.h"

class ACataclysmCharacterBase;

/** What one pass of the controller's thinking decided to do. */
UENUM(BlueprintType)
enum class ECataclysmBrainAction : uint8
{
	/** Nothing hostile within sight. Standing still. */
	Idle,

	/** Something hostile is in sight but out of reach. Walking toward it. */
	Chasing,

	/** In reach. Hitting it, or waiting for the attack interval to elapse. */
	Attacking,
};

/**
 * Decides who a monster or a summoned imp attacks, and walks it there.
 *
 * WHY IN C++ RATHER THAN A BEHAVIOUR TREE. A behaviour tree and its blackboard
 * are binary `.uasset` files. Every other rule in this project is text a pull
 * request can show a diff of, and every other behaviour is covered by an
 * automation test that runs headless. Three states -- idle, chase, attack --
 * expressed as a tree would be six assets nobody could review and nothing could
 * test, to say what twenty lines say here. A behaviour tree earns its cost when
 * the logic is deep enough that designers need to edit it without a programmer,
 * and issue #39's seven enemies are the point at which that is worth revisiting.
 *
 * IT THINKS ON A TIMER, NOT ON TICK. Four times a second. A dungeon floor can
 * hold a great many monsters, and asking "who is nearest" sixty times a second
 * for each of them is a sphere overlap per monster per frame for an answer that
 * does not change that fast. The same reasoning already applies to
 * ACataclysmGroundZone's sweep.
 *
 * WHAT IT DOES NOT DO. It has no memory: it re-picks the nearest target every
 * pass rather than staying with one, so two monsters equally distant can swap
 * between them. It has no leash, so a monster that has noticed the player
 * follows for as long as the player stays within its sight radius rather than
 * returning to where it started. Diablo II gives each monster its own vision
 * distance and Path of Exile monsters break off and return when the player gets
 * far enough, so both of those are shapes worth having; neither is needed to
 * make an imp chase what it is attacking, which is what issue #163 asks for.
 */
UCLASS()
class CATACLYSM_API ACataclysmEnemyController : public AAIController
{
	GENERATED_BODY()

public:
	ACataclysmEnemyController();

	/** Seconds between one pass of the thinking and the next. */
	static constexpr float ThinkIntervalSeconds = 0.25f;

	/**
	 * How near the target the walk aims for, as a fraction of the attack reach.
	 *
	 * Short of the reach rather than exactly at it, so that a target which
	 * shuffles slightly does not put the pawn just outside its own reach and
	 * make it stop and start.
	 */
	static constexpr float ApproachFractionOfReach = 0.8f;

	/**
	 * Choose a target, walk toward it, and hit it when it is in reach.
	 *
	 * Public and callable so that tests can run one pass without waiting for a
	 * timer, in the same way ACataclysmMinion::AttackOnce is.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|AI")
	ECataclysmBrainAction Think();

	/**
	 * The nearest thing the possessed pawn may attack, or null.
	 *
	 * Nearest rather than chosen. Which side something is on is
	 * UCataclysmTeams's question, and it is what makes a maddened monster's
	 * neighbours legal targets: Madness makes an actor hostile to everything, so
	 * this search starts returning them without knowing anything about Madness.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|AI")
	AActor* ChooseTarget() const;

	/** What the last pass of Think decided. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	ECataclysmBrainAction LastAction = ECataclysmBrainAction::Idle;

	/** What the last pass of Think was going after. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	TObjectPtr<AActor> CurrentTarget;

	/** How many times it has told its pawn to attack. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 AttacksOrdered = 0;

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The possessed pawn as the class this controller knows how to drive. */
	ACataclysmCharacterBase* Body() const;

private:
	FTimerHandle ThinkTimer;

	/** World seconds at the last attack, so the interval can be honoured. */
	float LastAttackTime = 0.0f;

	/** False until the first attack, so the first one is never made to wait. */
	bool bHasAttacked = false;
};
