// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "CataclysmProjectile.generated.h"

class ACataclysmProjectile;

/** Told when a projectile has finished flying, and where it stopped. */
DECLARE_MULTICAST_DELEGATE_OneParam(FCataclysmProjectileFinished, ACataclysmProjectile*);

/**
 * A thrown or fired thing that occupies space between the caster and its target.
 *
 * WHAT THIS REPLACED AND WHY IT MATTERED. `UCataclysmProjectileSkill` turned a
 * Speed into a delay: it worked out `distance / speed`, waited that long on a
 * timer, and then resolved the whole hit at once using positions at the moment
 * of impact. Nothing occupied the space in between, so who was hit was decided
 * entirely by where everyone stood at the end. An enemy that stepped into the
 * path after the throw and out of it before the landing was never touched, and
 * one that stepped in just before the landing was hit even though the projectile
 * had already passed behind it. A wall between the caster and the destination
 * did not stop anything. Issue #164.
 *
 * IT MOVES IN STEPS AND SWEEPS EACH STEP, rather than testing where it is. A
 * step at 2000 centimetres per second and 60 frames a second is 33 centimetres,
 * which is smaller than a character but not by much, so testing only the end of
 * a step would let a thin target fall between two of them. Each step therefore
 * asks who is inside the capsule from where it was to where it now is, which is
 * the volume it actually passed through.
 *
 * PIERCE DECIDES WHAT A HIT DOES TO IT. One that pierces passes through and
 * keeps its speed, hitting each enemy once, until its pierce budget runs out.
 * One that does not pierce stops at the first enemy it touches and hits in a
 * radius there. All four designed piercing skills are written `Pierce=99`, which
 * no real fight exhausts, so in practice they cross the whole range.
 *
 * WORLD GEOMETRY STOPS IT. A line trace on the visibility channel runs over the
 * same step, and a blocking hit ends the flight at the impact point. That is
 * what makes cover mean something.
 *
 * NO MESH AND NO PARTICLE EFFECT. This project has no art content, so the
 * projectile is invisible and what it hits is the only evidence it exists. That
 * is a content gap, not a behaviour gap, and it is the same gap
 * `ACataclysmGroundZone` has.
 *
 * WHY IT DEALS ITS OWN DAMAGE rather than calling back into the ability that
 * fired it. A projectile can outlive its caster, and an ability that ended
 * cannot be asked to hit anything. Everything it needs -- the damage percent,
 * the tags that scope the caster's modifiers, and whether it sets what it hits
 * alight -- is copied onto it when it is fired.
 */
UCLASS()
class CATACLYSM_API ACataclysmProjectile : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmProjectile();

	/**
	 * Fire one. It starts moving on the next tick.
	 *
	 * @param Instigator     who fired it. It never hits them, and it takes their side.
	 * @param From           where it starts
	 * @param To             where it is aimed. The direction and the range come from this.
	 * @param InRadiusCm     how wide it is
	 * @param InSpeed        centimetres per second. Zero or less spawns nothing.
	 * @param InPierce       how many enemies it passes THROUGH. Zero stops at the first.
	 * @param bInReturns     whether it comes back to where it started
	 * @param InDamagePercent percent of the caster's weapon damage one hit deals
	 * @param InSkillTags    the firing skill's tags, which scope the caster's modifiers
	 * @param bInBurns       whether it sets what it hits alight
	 * @return the projectile, or null if the world, the caster or the speed is missing
	 */
	static ACataclysmProjectile* Fire(AActor* Instigator, const FVector& From,
									  const FVector& To, float InRadiusCm,
									  float InSpeed, int32 InPierce, bool bInReturns,
									  float InDamagePercent,
									  const FGameplayTagContainer& InSkillTags,
									  bool bInBurns);

	/**
	 * Move forward by one step and hit whatever that step passed through.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT. The automation tests build a world with
	 * `UWorld::CreateWorld` and never tick it, which is why every other template
	 * exposes its one repetition the same way -- `SwingOnce` on a strike,
	 * `Pulse` on an aura, `SummonOne` on a summon.
	 *
	 * @return true while it is still flying, false once it has finished
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Projectile")
	bool Step(float DeltaSeconds);

	/** Broadcast once, when it stops. The firing skill listens so it can leave ground. */
	FCataclysmProjectileFinished OnFinished;

	/** Where it was fired from. One end of the path it burned, if it burns one. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	FVector StartedAt = FVector::ZeroVector;

	/**
	 * The furthest it got from where it was fired.
	 *
	 * THE OTHER END OF THE PATH IT BURNED, and not the same as where it ended
	 * up. Emberhurl "returns to your hand", so a returning projectile finishes
	 * back at the caster; burning the line from where it started to where it
	 * finished would leave a patch at the caster's feet rather than the flight
	 * path the description promises.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	FVector FurthestReached = FVector::ZeroVector;

	/** How many legs it flew: one, or two when it came back. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Projectile")
	int32 LegsFlown() const { return bReturning ? 2 : 1; }

	/**
	 * What the skill's Radius parameter means for this projectile.
	 *
	 * IT MEANS TWO DIFFERENT THINGS, and which one depends on Pierce. For a
	 * piercing skill it is the HALF-WIDTH OF THE LINE it hits along: Emberhurl,
	 * Chain of Coals, Hellbrand and Infernal Lance are all written `Radius=1.5`,
	 * a metre and a half either side of the throw. For one that does not pierce
	 * it is the BLAST RADIUS where it stops: Blood Pyre is `Radius=3` and Magma
	 * Quake `Radius=4`, which are the sizes of the pyre and the crater, not the
	 * size of the thing in the air.
	 *
	 * BodyRadiusCm is how wide the flying object is, and the two are only the
	 * same for a piercing projectile.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float RadiusCm = 0.0f;

	/**
	 * How wide the flying object itself is, in centimetres.
	 *
	 * WHY IT IS NOT ALWAYS RadiusCm. Blood Pyre's `Radius=3` is a three metre
	 * blast. Using that as the width of the thing in the air would stop the
	 * projectile three metres short of the enemy it was thrown at, and the pyre
	 * would then go off in front of them rather than against them.
	 *
	 * NOT IN THE DATA, because the design never states it and it would be the
	 * same number written into 398 rows. It becomes a shape parameter the first
	 * time a skill needs its own.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float BodyRadiusCm = 0.0f;

	/**
	 * How wide a projectile is when its Radius means something else.
	 *
	 * Chosen against the collision it has to meet rather than picked freely: an
	 * enemy's capsule is 48 centimetres across the radius, so a 40 centimetre
	 * body is a little narrower than the thing it is trying to hit. Narrower
	 * means a projectile never touches somebody it visually passed beside;
	 * wider would make near misses land.
	 */
	static constexpr float DefaultBodyRadiusCm = 40.0f;

	/** Centimetres per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float SpeedCmPerSecond = 0.0f;

	/** How much further it may travel before it has reached its aimed point. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float RemainingRangeCm = 0.0f;

	/** How many more enemies it may pass through. One that does not pierce is zero. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	int32 PiercesLeft = 0;

	/** How many enemies it has hit, over its whole flight including any return. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	int32 EnemiesHit = 0;

	/** Whether it pierces at all. Decides whether stopping detonates in a radius. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	bool bPierces = false;

	/** Whether it is on its way back. Emberhurl "returns to your hand". */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	bool bReturning = false;

	/** Whether it has stopped. A finished projectile ignores further steps. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	bool bFinished = false;

	/** True when it stopped because world geometry blocked it. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	bool bBlockedByGeometry = false;

	virtual void Tick(float DeltaSeconds) override;

protected:
	/**
	 * An empty root so the actor has a position at all.
	 *
	 * NOT DECORATION, and this project has been caught by it twice. An actor
	 * whose components are all non-scene components has no root component, and
	 * an actor with no root component reports its location as the world origin
	 * however it was spawned. It carries no collision, because a projectile
	 * finds what it passes through by overlapping the world rather than by being
	 * overlapped.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Projectile")
	TObjectPtr<USceneComponent> Anchor;

private:
	/**
	 * How far along a step it actually got, stopping at world geometry.
	 *
	 * Sets bBlockedByGeometry when something solid was in the way.
	 */
	FVector TraceStep(const FVector& From, const FVector& To);

	/** Hit everyone in the capsule from Previous to Current, and say whether to stop. */
	bool HitAlongStep(const FVector& Previous, const FVector& Current);

	/** Deal one hit and set the target alight if this projectile does that. */
	void HitOne(AActor* Target);

	/** Stop flying, detonate if it does not pierce, and tell whoever is listening. */
	void Finish();

	/** Turn round and fly back to where it started, hitting again on the way. */
	void BeginReturn();

	/** Unit vector along the direction of travel, on the ground plane. */
	FVector Direction = FVector::ForwardVector;

	/** Whether it turns round at the far end. */
	bool bWillReturn = false;

	/** What one hit deals, as a percent of the caster's weapon damage. */
	float DamagePercent = 0.0f;

	/** The firing skill's tags, which decide which of the caster's modifiers apply. */
	FGameplayTagContainer SkillTags;

	/** Whether it sets what it hits alight. */
	bool bBurns = false;

	/**
	 * Whether Finish has already run.
	 *
	 * SEPARATE FROM bFinished because they mean different things. bFinished is
	 * set the moment the flight is over, including from inside HitAlongStep when
	 * the pierce budget runs out mid-step, and Step reads it to know to stop.
	 * This one guards the detonation and the broadcast, which must happen once.
	 */
	bool bFinishedAndSettled = false;

	/**
	 * Who it has already hit on this leg.
	 *
	 * PER LEG, NOT PER FLIGHT. Emberhurl "hits once going out and once returning
	 * to your hand", so this is cleared when it turns round. Within one leg it
	 * stops the same enemy being hit on every step while the projectile is
	 * still inside them.
	 */
	TSet<TWeakObjectPtr<AActor>> AlreadyHit;
};
