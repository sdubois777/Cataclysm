// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmTether.generated.h"

/**
 * A burning line holding two creatures within a stated distance of each other.
 *
 * WHAT ASKS FOR IT. The Whip's Tether: "Bind two enemies within 12 meters
 * together with a burning line. Neither can move more than 5 meters from the
 * other, the line sets alight anything that touches it, and if either tries to
 * break away both are dragged back together." Its row states `TetherTargets=2`,
 * `TetherLength=5` and `TetherDuration=8`, and until 2026-09-02 nothing read any
 * of the three.
 *
 * AN ACTOR RATHER THAN A COMPONENT ON EACH END, WHICH IS THE OPPOSITE CHOICE
 * FROM `UCataclysmPinnedLine`. That one records a fact -- these creatures were
 * pinned by one throw -- and does nothing until somebody dies, so a component
 * destroyed with its owner is exactly right. This one has to keep acting: it
 * measures a distance, moves two creatures and burns whatever is between them,
 * several times a second. Putting that on both ends would mean two copies of one
 * timer doing the same work and disagreeing whenever one of them ran first.
 *
 * IT OUTLIVES THE SKILL THAT MADE IT, which is the argument
 * `UCataclysmCurseSpread` gives. Tether is a Projectile and ends in the frame its
 * bolt lands; the line it made holds for eight seconds afterwards.
 *
 * IT DESTROYS ITSELF WHEN EITHER END IS GONE. A tether to a corpse is not a
 * tether, and the row says nothing about the survivor staying bound to anything.
 * `SetLifeSpan` handles the ordinary case of the eight seconds running out.
 */
UCLASS()
class CATACLYSM_API ACataclysmTether : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmTether();

	/**
	 * How often the line measures itself, in seconds.
	 *
	 * FOUR TIMES A SECOND, WHICH IS A COMPROMISE BETWEEN TWO FAILURES. Checking
	 * once a second -- the rate `UCataclysmSkillEffects::BaseSecondsPerTick`
	 * uses for damage over time -- would let a creature run most of a second past
	 * the limit and then snap back, which reads as a bug rather than as a rope.
	 * Checking every frame would rebuild the burning effect sixty times a second
	 * for every creature standing on the line, and a burn is single-stack so
	 * fifty-nine of those would be refreshing what the sixtieth had just applied.
	 *
	 * NOT A PER-FRAME TICK, FOR THE REASON `ACataclysmGroundZone` GIVES: the work
	 * is a sweep, and a tick would run it sixty times more often for the same
	 * result.
	 */
	static constexpr float SecondsPerCheck = 0.25f;

	/**
	 * Bind creatures together and return the line, or null if it could not be.
	 *
	 * EXACTLY TWO, AND MORE THAN TWO IS REFUSED RATHER THAN TRUNCATED. "Neither
	 * can move more than 5 meters from the other" is a sentence about a pair, and
	 * three creatures on one line would need a rule about which distances are
	 * measured that the row does not give. The caller decides how many to gather;
	 * `TetherTargets` is that number and it states 2.
	 *
	 * FEWER THAN TWO IS THE ORDINARY FAILURE and it is not an error. A Tether
	 * cast at a lone enemy has nothing to bind it to, so it binds nothing and the
	 * bolt still flies and still burns what it hits.
	 *
	 * @param Caster            whose skill this is. Kept so the burning line is
	 *                          credited to the player and so it knows whose
	 *                          enemies to look for along itself
	 * @param Pair              the two creatures. Anything invalid is refused
	 * @param MaxSeparationCm   how far apart they may get before being dragged
	 *                          back. `TetherLength`
	 * @param DurationSeconds   how long the line holds. `TetherDuration`
	 * @param LineHalfWidthCm   how close something must come to the line to be
	 *                          set alight by it. The skill's own `Radius`
	 */
	static ACataclysmTether* Bind(AActor* Caster, const TArray<AActor*>& Pair,
								  float MaxSeparationCm, float DurationSeconds,
								  float LineHalfWidthCm);

	/**
	 * One measurement of the line: drag if it is stretched, burn what touches it.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT. A world built by `UWorld::CreateWorld` is
	 * never ticked and its timer manager never fires, which is the same reason
	 * `UCataclysmSelfBuffSkill::RepeatTick` and `UCataclysmGroundZone::Sweep` are
	 * public.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Tether")
	void Check();

	/** Whether both ends are still alive, so the line still holds. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Tether")
	bool IsHolding() const;

	/** How far apart the two ends are right now, along the ground. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Tether")
	float SeparationCm() const;

	/** How many checks have had to drag the two ends back. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Tether")
	int32 TimesDragged = 0;

	/** How many bystanders the last check set alight. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Tether")
	int32 LastCheckLit = 0;

	/** One end of the line. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Tether")
	TWeakObjectPtr<AActor> First;

	/** The other. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Tether")
	TWeakObjectPtr<AActor> Second;

protected:
	virtual void BeginPlay() override;

private:
	/** Whose skill made it. The burn is credited to this character. */
	UPROPERTY()
	TWeakObjectPtr<AActor> Caster;

	/** How far apart the two ends may get. `TetherLength`. */
	float MaxSeparationCm = 0.0f;

	/** How close something must come to the line to be set alight. */
	float LineHalfWidthCm = 0.0f;

	FTimerHandle CheckTimer;

	/**
	 * Gives the actor a position at all.
	 *
	 * For the reason `ACataclysmGroundZone` states: without a root component an
	 * actor has no transform, so every one of them would sit at the world origin.
	 */
	UPROPERTY()
	TObjectPtr<class USceneComponent> Anchor;
};
