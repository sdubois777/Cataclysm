// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmGroundZone.generated.h"

/**
 * A patch of burning ground that hurts what stands in it, then goes away.
 *
 * A RIDER ON A SHAPE, NOT A SHAPE. Eight of the sixteen designed Demonic skills
 * leave one of these behind on top of whatever else they do: Molten Cleave
 * "drags a line of molten slag", Emberhurl leaves "its flight path burning",
 * Infernal Plunge leaves "a pool of lava". Issue #37's own table of shapes says
 * "persistent ground zone: used by most of the above", where every other entry
 * names the skills that use it -- which is the hint that it is a component of
 * the others rather than a seventh peer.
 *
 * IT RE-TESTS WHO IS INSIDE ON EVERY TICK rather than remembering who was there
 * when it was created. Standing in it is the cost, so walking out has to stop
 * it and walking in has to start it.
 *
 * IT IS A CAPSULE, NOT A CIRCLE. A zone has a start, a far end and a width, and
 * a circle is the case where the two ends are the same point. That is what lets
 * Emberhurl leave "its flight path burning" as one actor along the whole throw
 * rather than one patch where the axe stopped. Issue #167. The two cases share
 * one search because UCataclysmTargeting::IsInLine already treats a segment of
 * no length as a circle at its start, so there is no branch to get wrong.
 *
 * NO MESH AND NO PARTICLE EFFECT. This project has no art content at all -- 14
 * data tables, 10 input actions and one map -- so the zone is invisible and its
 * effect is the only evidence it is there. That is a content gap, not a
 * behaviour gap.
 */
UCLASS()
class CATACLYSM_API ACataclysmGroundZone : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmGroundZone();

	/**
	 * Put a round one in the world.
	 *
	 * @param Owner        whose skill left it; it never hurts them
	 * @param Location     where the centre is
	 * @param RadiusCm     how wide
	 * @param Duration     how long before it goes away
	 * @param DamagePerTick how much each enemy inside takes per tick
	 * @return the zone, or null if any of the numbers is not positive
	 */
	static ACataclysmGroundZone* Spawn(AActor* Owner, const FVector& Location,
									   float RadiusCm, float Duration,
									   float DamagePerTick);

	/**
	 * Put a long one in the world, covering everything within HalfWidthCm of the
	 * segment from Start to End.
	 *
	 * ONE ACTOR, NOT A ROW OF THEM. A chain of overlapping circles laid along the
	 * path would reuse Spawn unchanged, but it spawns as many actors as the path
	 * is long over the spacing, each with its own timer and its own sweep, and an
	 * enemy standing where two of them overlap takes two ticks a second instead
	 * of one. Path of Exile's Flame Wall -- the shipped ground effect nearest to
	 * this -- is a single line-shaped area, and its own rule is that an enemy can
	 * only be damaged by one Flame Wall at a time.
	 *
	 * @param HalfWidthCm  how far either side of the line it reaches
	 * @param bBurnsEveryone  whether it burns whatever is standing in it rather
	 *                        than only the owner's enemies -- **including the
	 *                        owner**. One thing in the design needs it: the
	 *                        Hellhound's burning lane, whose model entry says
	 *                        "The fire burns other enemies and the Hellhound
	 *                        itself". Everything else leaves a zone that knows
	 *                        whose side it is on.
	 */
	static ACataclysmGroundZone* SpawnAlong(AActor* Owner, const FVector& Start,
											const FVector& End, float HalfWidthCm,
											float Duration, float DamagePerTick,
											bool bBurnsEveryone = false);

	/**
	 * Whether it burns whatever is standing in it, including its own owner.
	 *
	 * FALSE FOR EVERY ZONE BUT ONE. A skill's ground effect belongs to whoever
	 * cast it and hurts the other side; this is the exception the Hellhound's
	 * design asks for, and it is a field rather than a separate class because
	 * the only thing that differs is which search the sweep runs.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	bool bBurnsEveryone = false;

	/** Seconds between one sweep of who is standing in it and the next. */
	static constexpr float TickSeconds = 1.0f;

	/** How wide it is, in centimetres. Its radius when round, half its width when long. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float RadiusCm = 0.0f;

	/**
	 * The far end, in world space. Equal to the actor's own location when round.
	 *
	 * The near end is the actor's own location, so a zone is where it says it is
	 * in the outliner and its two ends cannot disagree.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	FVector FarEnd = FVector::ZeroVector;

	/** Whether it covers a path rather than a point. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Ground Zone")
	bool IsLong() const { return !FarEnd.Equals(GetActorLocation()); }

	/** What one tick deals to each enemy inside. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float DamagePerTick = 0.0f;

	/** How many times it has swept. Read by tests; there is nothing else to see. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	int32 TicksElapsed = 0;

	/** How many enemies the last sweep found. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	int32 LastSweepCount = 0;

	/** Burn everything standing in it now. Called on a timer, and by tests. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Ground Zone")
	void Sweep();

protected:
	virtual void BeginPlay() override;

	/**
	 * An empty root so the actor has a position at all.
	 *
	 * NOT DECORATION. An actor whose components are all non-scene components has
	 * no root component, and an actor with no root component reports its
	 * location as the world origin however it was spawned. This class had no
	 * components at all, so every patch of burning ground in the project swept
	 * around (0,0,0) rather than around where the skill left it. It carries no
	 * collision: a zone finds who is standing in it by overlapping the world, so
	 * nothing ever needs to overlap the zone itself.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Ground Zone")
	TObjectPtr<USceneComponent> Anchor;

private:
	/** Its lifetime is SetLifeSpan; only the sweep needs a timer of its own. */
	FTimerHandle SweepTimer;
};
