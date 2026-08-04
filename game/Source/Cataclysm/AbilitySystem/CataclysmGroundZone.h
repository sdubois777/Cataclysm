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
	 * Put one in the world.
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

	/** Seconds between one sweep of who is standing in it and the next. */
	static constexpr float TickSeconds = 1.0f;

	/** How wide it is, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float RadiusCm = 0.0f;

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

private:
	/** Its lifetime is SetLifeSpan; only the sweep needs a timer of its own. */
	FTimerHandle SweepTimer;
};
