// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "CataclysmProjectile.generated.h"

class ACataclysmProjectile;
class UStaticMeshComponent;

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
 * A LOB IS REAL PROJECTILE MOTION, AND THAT IS A STATEMENT ABOUT SPEED, NOT
 * ABOUT SHAPE. Issue #465. The shape was already a parabola; what was wrong was
 * how the projectile moved along it. The old step held the speed THROUGH THE
 * AIR constant, so the steeper the path the less ground it covered per second:
 * over a ten metre throw from a hand to the floor its horizontal speed was 80%
 * of the figure at launch, 97% halfway and 62% at the landing, and over a two
 * metre one, 97% and 41%. It crossed the ground early and then sank slowly onto
 * the marker, which the project owner reported from play. Gravity acts downward
 * and nothing acts sideways, so a thrown object holds its HORIZONTAL speed and
 * lets only the vertical one change -- meaning it covers ground at a steady
 * rate and its descent speeds up. That is what happens here now.
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
 * WHAT IT LOOKS LIKE IS THE CALLER'S BUSINESS. It carries a placeholder sphere
 * from engine content, which is what every player skill still uses, and `Fire`
 * takes an optional mesh for a caster that has one. The Brute passes the rock
 * out of the Paragon pack. Baking a mesh into this class instead would give a
 * fire bolt and a thrown boulder the same appearance, because all 398 rows of
 * `game/Data/WeaponSkills.csv` come through here. Issues #403 and #404.
 *
 * IT ALSO CARRIES A PARTICLE EFFECT, and that one IS baked in rather than being
 * the caller's business, because it is the same shape for every projectile:
 * `UCataclysmProjectileEffect::AttachTo` gives it the glowing head and the trail
 * of `NS_Proj_Body`, coloured from the firer's damage type. One asset serves all
 * eight types, so a Demonic bolt and a Void bolt differ by a data row rather
 * than by an asset -- see `docs/Niagara_Conventions.md` section 5.
 * `ACataclysmGroundZone` still has the gap this one used to have.
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
	 * @param InBodyMesh     what the flying object looks like. Null keeps the
	 *   placeholder sphere, which is what every player skill uses today.
	 * @param InFlightSeconds how long a LOBBED shot stays in the air. Zero, which
	 *   is what every player skill passes, means it does not lob: it travels
	 *   flat at InSpeed. Above zero it follows real projectile motion onto
	 *   `To` and InSpeed is not used, because a ballistic shot does not have
	 *   one speed. See ApexHeightCm.
	 * @param InCritChancePercent the firing skill's own base critical strike
	 *   chance. -1, the default, means the skill states none and the hit takes
	 *   the firer's own attribute, which is what an enemy's thrown rock does and
	 *   what every player skill does today. Issue #657.
	 * @param InSkillHealthCostPercent what the firing skill cost, as a share of
	 *   the firer's maximum health. -1, the default, means no skill cost is
	 *   known, which is what an enemy's thrown rock passes. Issue #983.
	 * @return the projectile, or null if the world or the caster is missing, or if
	 *   it was given neither a speed nor a flight time
	 */
	static ACataclysmProjectile* Fire(AActor* Instigator, const FVector& From,
									  const FVector& To, float InRadiusCm,
									  float InSpeed, int32 InPierce, bool bInReturns,
									  float InDamagePercent,
									  const FGameplayTagContainer& InSkillTags,
									  bool bInBurns,
									  UStaticMesh* InBodyMesh = nullptr,
									  float InFlightSeconds = 0.0f,
									  float InCritChancePercent = -1.0f,
									  float InSkillHealthCostPercent = -1.0f);

	/**
	 * Swap what the flying object looks like, and size it to BodyRadiusCm.
	 *
	 * CHOSEN BY WHOEVER FIRES, NEVER BAKED IN. This class is generic: every
	 * projectile skill in game/Data/WeaponSkills.csv uses it, so giving it a
	 * rock in its constructor would make every player fire bolt a rock. The
	 * Brute passes its own; nothing else passes anything and keeps the sphere.
	 * Issue #404.
	 *
	 * SIZED FROM THE MESH'S OWN BOUNDS, not from an assumption about how big it
	 * is. The engine's basic shapes occupy a 100 centimetre cube and a mesh out
	 * of an art pack does not, so a scale worked out for one is meaningless for
	 * the other. Both come out at BodyRadiusCm, which is the width the sweep
	 * uses, so what the player sees and what hits them stay the same size.
	 *
	 * A null mesh leaves whatever is already there.
	 */
	void SetBodyMesh(UStaticMesh* Mesh);

	/**
	 * The tags of the skill that fired this, which include its `Element.*`.
	 *
	 * READ ONLY, AND DELIBERATELY NOT A SETTER. The tags are decided once by
	 * `Fire` and a projectile whose firing skill could be changed in flight
	 * would resolve its damage against modifiers that were not in play when it
	 * was thrown.
	 *
	 * WHAT READS IT FROM OUTSIDE. `UCataclysmProjectileEffect::DamageTypeFor`,
	 * to draw a player's bolt in the damage type its skill row names instead of
	 * the authored white. Issue #803.
	 */
	const FGameplayTagContainer& FiringSkillTags() const { return SkillTags; }

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

	/**
	 * Centimetres per second ACROSS THE GROUND, and it does not change during
	 * the flight.
	 *
	 * FOR A FLAT SHOT it is simply the speed it was fired at, which is the
	 * whole of its motion.
	 *
	 * FOR A LOB it is worked out rather than given: the horizontal distance
	 * divided by FlightSeconds. That is not a detail of bookkeeping, it is the
	 * definition of projectile motion. Gravity acts downward and nothing acts
	 * sideways, so the horizontal component of a thrown object's velocity is
	 * constant from launch to landing while only the vertical one changes.
	 * Issue #465.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float SpeedCmPerSecond = 0.0f;

	/** How much further it may travel before it has reached its aimed point. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float RemainingRangeCm = 0.0f;

	/**
	 * How long a lobbed shot is in the air, in seconds. **Zero means it does
	 * not lob at all**, which is what every player skill passes and what this
	 * class did for everything before issue #459.
	 *
	 * WHY OPT-IN RATHER THAN ALWAYS ON. All 398 rows of
	 * `game/Data/WeaponSkills.csv` fire through this class. A lob applied to
	 * every one of them would turn each fire bolt into a mortar, so a caller
	 * that wants one asks for it, exactly as the Brute passes its own mesh
	 * rather than the class knowing about rocks.
	 *
	 * WHY TIME IS THE NUMBER GIVEN, rather than a speed or an angle or an
	 * apex. A ballistic solve needs exactly one input beyond the two ends and
	 * gravity, and the four candidates are not equivalent. Time is the one that
	 * matters for a TELEGRAPHED attack: the marker appears, and the player's
	 * window to leave is the wind-up plus the flight. Fixing the flight makes
	 * that window a designed number instead of an accident of how far away the
	 * player happened to be standing. Shipped games choose this deliberately --
	 * the Old School RuneScape wiki documents most ranged attacks scaling their
	 * hit delay with distance and specific weapons given "a fixed hit delay of
	 * 2 ticks regardless of distance" -- and Diablo 4's ground-circle attacks
	 * land a set moment after the circle appears. Issue #465.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float FlightSeconds = 0.0f;

	/**
	 * How high above the straight line from launch to landing it rises, in
	 * centimetres. Zero for a shot that does not lob.
	 *
	 * DERIVED, NOT GIVEN, since issue #465. It is `g * t * t / 8`, which is how
	 * far a parabola sags below its own chord over a flight of t seconds. Two
	 * things follow that are worth knowing before reading a number off it:
	 *
	 * IT IS THE SAME HEIGHT AT EVERY RANGE, because it depends only on the
	 * flight time. A two metre lob and a ten metre lob taking the same time
	 * rise the same distance above their chords, so the short one looks steep
	 * and the long one shallow. That is what real thrown objects do.
	 *
	 * IT IS NOT SEPARATELY TUNABLE. Before #465 the apex was passed in and the
	 * flight time fell out of it; now the flight time is passed in and the apex
	 * falls out. There is one knob rather than two because gravity ties them
	 * together, and inventing a second would mean inventing a gravity.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Projectile")
	float ApexHeightCm = 0.0f;

	/**
	 * The downward acceleration a lobbed shot falls under, in centimetres per
	 * second squared.
	 *
	 * UNREAL'S OWN DEFAULT, which is `DefaultGravityZ=-980.0` in
	 * `Engine/Config/BaseEngine.ini`, so a thrown rock falls at the rate
	 * everything else in the engine falls at.
	 *
	 * A CONSTANT RATHER THAN THE WORLD'S GRAVITY, deliberately. A projectile
	 * fired by a telegraphed attack has to arrive when the telegraph promised,
	 * and reading `UWorld::GetGravityZ` would let a level with altered gravity
	 * silently change how high every enemy's lob rises. The flight time is a
	 * design number; this is what turns it into a shape.
	 *
	 * MASS DOES NOT APPEAR HERE AND NOTHING IS MISSING. In a vacuum every
	 * object falls at the same rate whatever it weighs, so a projectile weight
	 * would change nothing about where this rock goes or when it arrives.
	 * Unreal agrees: `UProjectileMovementComponent` -- the engine's own
	 * projectile mover -- contains no reference to mass at all, and even its
	 * `AddForce` is added straight to acceleration without being divided by
	 * one. What reads as weight is the shape of the arc and the fact that the
	 * descent accelerates, which is what this produces.
	 */
	static constexpr float LobGravityCmPerSecondSquared = 980.0f;

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

	/**
	 * A stand-in for the flying object, sized to BodyRadiusCm when it is fired.
	 *
	 * WITHOUT THIS EVERY PROJECTILE IN THE GAME IS INVISIBLE, and every one of
	 * them was. This class had exactly one component -- an empty scene component
	 * to root it -- so a thrown rock, a bolt of fire and every other projectile
	 * skill dealt their damage with nothing on screen travelling between the
	 * caster and the target. Reported from a play session on 2026-08-08 as the
	 * Brute's rock throw not throwing a rock; it was never specific to the Brute.
	 *
	 * A SPHERE FROM ENGINE CONTENT, found by path, so this adds no asset to the
	 * project. The player, every enemy and the summoned imp all carry a
	 * placeholder of this kind for the same stated reason: this project's own
	 * Content folder holds no meshes. A failure to find it is not fatal -- the
	 * projectile still flies and still deals damage, it is just invisible again.
	 *
	 * NO COLLISION, like every other placeholder here. A projectile finds what it
	 * passes through by sweeping the world itself, and a colliding mesh would
	 * give it a second, differently sized way to hit things.
	 *
	 * PUBLIC LIKE THE SUMMONED IMP'S, so a test can check that a fired
	 * projectile actually has something to draw.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Projectile")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

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

	/**
	 * The whole horizontal distance from launch to landing, in centimetres.
	 *
	 * Kept because `RemainingRangeCm` counts down and an arc needs to know how
	 * far ALONG the flight it is, which is the one minus the other.
	 */
	float TotalRangeCm = 0.0f;

	/** The height it was launched from and the height it lands at, in that order. */
	float LaunchZ = 0.0f;
	float LandingZ = 0.0f;

	/**
	 * Where a lobbed projectile should be, given how far it has travelled
	 * horizontally.
	 *
	 * Returns the height only, and taking horizontal distance rather than
	 * elapsed time is not a shortcut: the two are interchangeable here because
	 * a lob's horizontal speed is constant, so the fraction of the ground it
	 * has covered IS the fraction of the flight that has passed. Working from
	 * distance keeps the landing point exact rather than the accumulation of
	 * however many steps the frame rate happened to produce.
	 */
	float ArcHeightAfter(float HorizontalTravelledCm) const;

	/** Whether it turns round at the far end. */
	bool bWillReturn = false;

	/** What one hit deals, as a percent of the caster's weapon damage. */
	float DamagePercent = 0.0f;

	/** The firing skill's tags, which decide which of the caster's modifiers apply. */
	FGameplayTagContainer SkillTags;

	/**
	 * The firing skill's own base critical strike chance, or -1 for none stated.
	 *
	 * CARRIED RATHER THAN READ WHEN IT LANDS, for the same reason the tags above
	 * are: a projectile arrives after the skill that fired it has finished, so
	 * the character may be holding a different skill by then. Issue #657.
	 */
	float CritChancePercent = -1.0f;

	/**
	 * What the firing skill cost, as a share of the firer's maximum health,
	 * or -1 for a projectile with no skill cost behind it.
	 *
	 * CARRIED RATHER THAN READ WHEN IT LANDS, for the same reason the critical
	 * strike chance above is: a projectile arrives after the skill that fired
	 * it has finished, and the character may have used something else by then.
	 * Reading the skill's cost at impact would credit the blow with whatever
	 * the character last paid rather than with what this shot cost.
	 * Issue #983.
	 */
	float SkillHealthCostPercent = -1.0f;

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
