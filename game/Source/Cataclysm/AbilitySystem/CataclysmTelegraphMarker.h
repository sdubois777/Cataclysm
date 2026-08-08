// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmTelegraphMarker.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * The patch of ground an enemy's attack is about to land on, drawn while it
 * winds up and taken away when it lands.
 *
 * WHAT IT IS FOR. The design's wind-up rule is
 *
 *     Wind-up seconds = 0.4 + Radius / 3.5
 *
 * where 0.4 is a reaction allowance and 3.5 metres per second is the slowest
 * class's walk speed. That formula is a promise that the player can see an area
 * and walk out of it in time. Until this existed nothing in the project drew any
 * area at all, so the promise was being kept on the timing side and broken on
 * the seeing side: the player had to judge a three and a half metre radius from
 * an animation. Issue #396.
 *
 * IT IS DRAWN FROM THE ABILITY'S OWN NUMBERS AND NEVER AUTHORED SEPARATELY.
 * FCataclysmEnemyAbility carries the radius the marker uses and the ability's
 * own code uses the same figure, so the two cannot disagree. A marker that
 * showed a different circle from the one that hurts would be worse than no
 * marker, because the player would have learnt to trust it.
 *
 * TWO SHAPES, BECAUSE THE DESIGN HAS TWO. A Strike marks a circle around the
 * creature. A Projectile marks the lane it will fly down: a rectangle of width
 * twice the projectile's radius, running from the creature to where the shot
 * was aimed. Aura and Movement are also telegraphed shapes in
 * sim/cataclysm_sim/enemy_abilities.py and no enemy in the project has either
 * yet, so neither is built here.
 *
 * NO MATERIAL AND NO PARTICLE SYSTEM. This project's own Content folder holds
 * no materials and no particle assets, so a marker built from either would be
 * the first authored art asset in the repository and would land in Git LFS. It
 * is built instead from /Engine/BasicShapes, exactly as the placeholder bodies
 * on the player, the enemies and the projectiles already are: a flattened
 * cylinder for a circle and a flattened cube for a lane. That reads correctly
 * and costs the repository nothing. Replacing it with a decal or a Niagara
 * system is a content change and does not touch any of the behaviour here.
 */
UCLASS()
class CATACLYSM_API ACataclysmTelegraphMarker : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmTelegraphMarker();

	/**
	 * A marker smaller than this is not drawn at all, in centimetres.
	 *
	 * ONE METRE, AND IT IS A DESIGN RULE RATHER THAN A TIDINESS ONE. The Attack
	 * Telegraphs subsection of the design document says a marker smaller than a
	 * metre "is smaller than the creature standing in it, so there is nowhere to
	 * walk". SMALLEST_USEFUL_MARKER_METRES in
	 * sim/cataclysm_sim/enemy_abilities.py holds the same figure and
	 * tools/tests/test_telegraph_markers.py pins the two together.
	 *
	 * The Brute's ordinary slam is the case this exists for: it reaches 0.9
	 * metres, so it draws nothing and is read off the creature instead.
	 */
	static constexpr float SmallestUsefulRadiusCm = 100.0f;

	/** How thick the drawn patch is, in centimetres. Enough to be visible on the
	 *  floor without standing up far enough to read as an object. */
	static constexpr float MarkerThicknessCm = 4.0f;

	/**
	 * Draw a circle on the ground and take it away after Seconds.
	 *
	 * @param Caster    whose attack it warns of; becomes the actor's owner
	 * @param Centre    the middle of the circle, at the height it is drawn at
	 * @param RadiusCm  the ability's own radius
	 * @param Seconds   the ability's wind-up
	 * @return the marker, or null if it was refused. It is refused for a radius
	 *   below SmallestUsefulRadiusCm, for a wind-up that is not positive, and
	 *   for no caster or no world -- all of which mean there is nothing useful
	 *   to draw rather than that something failed.
	 */
	static ACataclysmTelegraphMarker* ShowCircle(AActor* Caster,
												 const FVector& Centre,
												 float RadiusCm, float Seconds);

	/**
	 * Draw the lane from Start to End and take it away after Seconds.
	 *
	 * IT RUNS TO WHERE THE SHOT WAS AIMED, NOT TO THE ABILITY'S MAXIMUM RANGE.
	 * ACataclysmProjectile::Fire sets RemainingRangeCm from the distance between
	 * the two points it is given, so a projectile stops where it was aimed. A
	 * marker drawn out to the ability's full range would cover ground that
	 * nothing is going to happen on, which teaches the player to distrust it.
	 *
	 * @param HalfWidthCm  the projectile's own radius, so the lane is exactly as
	 *   wide as the thing that will travel down it
	 */
	static ACataclysmTelegraphMarker* ShowLine(AActor* Caster,
											   const FVector& Start,
											   const FVector& End,
											   float HalfWidthCm, float Seconds);

	/**
	 * Take it away now.
	 *
	 * Called when the attack lands and when a wind-up is abandoned. Safe on a
	 * marker that has already gone.
	 */
	void Dismiss();

	/** How wide it is, in centimetres. Its radius when round, half its width
	 *  when it is a lane. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Telegraph")
	float RadiusCm = 0.0f;

	/** How long the lane is, in centimetres. Zero for a circle, which is what
	 *  tells the two apart. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Telegraph")
	float LengthCm = 0.0f;

	/** Whether this marks a lane rather than a circle. Read by tests. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Telegraph")
	bool IsLane() const { return LengthCm > 0.0f; }

protected:
	/**
	 * The drawn patch.
	 *
	 * NO COLLISION, and that is not an optimisation. A marker is a warning about
	 * what is going to happen, not a thing in the world: one that blocked
	 * movement would stop the player walking out of the very area it is telling
	 * them to leave, and one that swept for overlaps could be hit by the attack
	 * it is warning about.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Telegraph")
	TObjectPtr<UStaticMeshComponent> Patch;

	/**
	 * The two engine shapes a marker is drawn with, found in the constructor.
	 *
	 * BOTH FOUND THERE AND NEITHER LATER, and that is a constraint of the engine
	 * rather than a preference. ConstructorHelpers::FObjectFinderOptional::Get
	 * calls CheckIfIsInConstructor, so reaching for the lane mesh from inside the
	 * static ShowLine below would assert. Held as members instead, so the lookup
	 * happens once when the class default object is built.
	 *
	 * A missing mesh is not fatal. The marker still spawns, still measures, and
	 * still goes away on time; it is simply invisible, exactly as the placeholder
	 * bodies elsewhere in this project handle the same case.
	 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> CircleMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> LaneMesh;
};
