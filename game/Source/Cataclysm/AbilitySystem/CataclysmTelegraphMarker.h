// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmTelegraphMarker.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
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
 * THE SHAPES ARE ENGINE CONTENT, THE MATERIAL IS OURS. The meshes are
 * /Engine/BasicShapes, exactly as the placeholder bodies on the player, the
 * enemies and the projectiles are: a flattened cylinder for a circle and a
 * flattened cube for a lane. The material is `/Game/Effects/M_TelegraphMarker`,
 * built by `tools/generate_telegraph_material.py`.
 *
 * IT IS UNLIT, AND THAT IS THE POINT OF HAVING ONE. Until issue #539 there was
 * no material at all, so both shapes took the engine default, which is lit. A
 * lit warning gets darker exactly when the world does, and the design commits to
 * a deliberately dark world in which the player has to see and dodge these
 * shapes. Section XIII of docs/Cataclysm_GDD_v2.md states the telegraph's
 * contrast against each of the eight Cataclysm themes, and those figures are
 * only true of the game if the shape's brightness comes from its own material.
 *
 * An earlier version of this comment said a material would be the repository's
 * first authored art asset and argued against one on that basis. That stopped
 * being true: game/Content already holds animation and DataTable assets, and
 * .gitattributes already routes .uasset to Git LFS.
 *
 * FOUR COLOURS, BECAUSE NO ONE COLOUR SURVIVES BOTH EXTREMES. Death and Void are
 * built on black and Celestial is gold and white, so a colour bright enough to
 * read on the first disappears into the second. Three opaque rings carry it
 * between them: the red ring carries the dark environments, the near-black rim
 * carries the bright ones, and the light inner line carries War's mid grey and
 * Demonic's lava. Measured across all eight themes the worst case is 3.92:1,
 * above the 3:1 accessibility threshold. The fourth colour is the see-through
 * fill, which carries none of that.
 *
 * An earlier version of this comment described a cyan fill, said there were two
 * tones, and quoted 3.22:1. All three were left behind by issue #543, which
 * replaced the cyan with red #FF3020 and added the third ring.
 *
 * THE FILL SWEEPS, AND THAT IS WHAT IT IS FOR. The design calls a telegraph "a
 * hard-edged geometric shape ... with a fill that sweeps as the wind-up runs
 * out", so the interior is how a player reads time-to-impact without watching
 * the creature. It grows from the middle of a circle, or from the caster's end
 * of a lane, and reaches the edge as the attack lands. Issue #544.
 *
 * NO PER-FRAME WORK IS DONE FOR IT. The marker sets a start time and a duration
 * on the fill's material once, when it is created, and the material works out
 * how far through the wind-up it is. See tools/generate_telegraph_material.py
 * for the arithmetic.
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
	 * The three ring widths, in centimetres, outermost first. Their sum is how
	 * far the marker's decoration extends past the area that actually hurts.
	 *
	 * WHY THERE ARE THREE RINGS AND NOT ONE. No single colour stays readable
	 * against all eight Cataclysm environments, because Death and Void are built
	 * on black while Celestial is gold and white. Measured against the extreme of
	 * each theme, the red ring alone reaches only 2.47:1 at worst and the
	 * near-black rim alone is worse; **together with a light inner line the worst
	 * case is 3.92:1**, above the 3:1 accessibility threshold for a graphical
	 * object that is not text.
	 *
	 * Which ring is doing the work changes with the ground. The near-black rim
	 * carries Celestial and Chaos, the light line carries War's mid grey and
	 * Demonic's lava, and the red ring carries the dark environments.
	 *
	 * THE WIDTHS ARE A JUDGEMENT, not a derivation. They total 18 cm against a
	 * smallest drawn marker of one metre radius. They are deliberately not scaled
	 * with the marker: a ring is meant to be seen at a constant thickness, and
	 * one that grew with the shape would be a band rather than an edge on a
	 * boss's six metre circle.
	 */
	static constexpr float RimDarkCm = 6.0f;
	static constexpr float RimBrightCm = 8.0f;
	static constexpr float RimLightCm = 4.0f;

	/** How far the rings reach past the area that hurts, in centimetres. */
	static constexpr float OutlineThicknessCm = RimDarkCm + RimBrightCm + RimLightCm;

	/**
	 * The four colours, as sRGB hex, matching section XIII of
	 * docs/Cataclysm_GDD_v2.md.
	 *
	 * KEPT AS HEX RATHER THAN AS LINEAR FLOATS so they can be read against the
	 * design document without conversion. FLinearColor::FromSRGBColor does the
	 * conversion where they are used, because a material parameter is linear.
	 *
	 * tools/tests/test_every_cataclysm_has_a_visual_theme.py holds the same
	 * values against the design document, so a change in one place that is not
	 * made in the other fails rather than going unnoticed.
	 */
	static const TCHAR* const DesignedOutlineHex;
	static const TCHAR* const DesignedRingHex;
	static const TCHAR* const DesignedInnerHex;

	/** The innermost band, which covers exactly the ground that hurts. The same
	 *  colour as the ring, drawn see-through. */
	static const TCHAR* const DesignedFillHex;

	/**
	 * How opaque the moving band is.
	 *
	 * WHY IT IS NOT 1. The project owner reported on 2026-08-13 that a fully
	 * opaque marker "is really solid", so the marked ground is tinted rather
	 * than covered and the floor still reads through it.
	 *
	 * WHY IT ROSE FROM 0.35 TO 0.6. The 0.35 was chosen when this band covered
	 * the whole marked area for the whole wind-up, where the objection was how
	 * much floor it hid. Since #544 it is a band a few tens of centimetres wide
	 * that crosses the marker once, so it hides almost nothing whatever its
	 * opacity, and 0.35 on a thin moving band is faint enough to lose against a
	 * busy floor. A JUDGEMENT, not a measurement, and one console command away:
	 *
	 *     Cataclysm.Telegraph.FillOpacity 0.35
	 *
	 * IT CARRIES NONE OF THE READABILITY, and that is why it is free to be
	 * light. A translucent band's contrast against the ground beneath it falls
	 * toward 1:1 as it fades -- at 0.25 over War's steel grey it is 1.54:1 -- so
	 * it could never have been the thing the guarantee rested on. The three
	 * opaque rings carry it.
	 */
	static constexpr float DesignedFillOpacity = 0.6f;

	/**
	 * How thick the moving band is, in centimetres.
	 *
	 * IN CENTIMETRES RATHER THAN AS A FRACTION OF THE MARKER, for the same
	 * reason the three ring widths are: a band meant to be seen at a constant
	 * thickness would become a wide swathe on a boss's six metre circle and a
	 * hairline on a one metre one. The marker divides this by its own size
	 * before handing it to the material, which works in fractions.
	 *
	 * A JUDGEMENT. The three static rings total 18 cm and read as an edge; this
	 * is the moving element and has to read as a band, so it is larger. It has
	 * not been tuned against anything and is one console command away:
	 *
	 *     Cataclysm.Telegraph.SweepBandCm 50
	 */
	static constexpr float DesignedSweepBandCm = 30.0f;

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

	/**
	 * The wind-up this marker was drawn for, in seconds. How long the fill has
	 * to sweep from the middle to the edge.
	 *
	 * HELD RATHER THAN READ BACK OFF THE ACTOR'S LIFESPAN, which is set to the
	 * same figure. The lifespan is a second guarantee that a marker goes away
	 * even if nothing dismisses it, so it is free to change independently; the
	 * sweep needs the ability's stated wind-up and should not silently start
	 * following a safety net instead.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Telegraph")
	float WindUpSeconds = 0.0f;

	/** Whether this marks a lane rather than a circle. Read by tests. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Telegraph")
	bool IsLane() const { return LengthCm > 0.0f; }

	/** The innermost band, covering the ground that hurts. Read by tests, which
	 *  check what it is coloured with, how opaque it is, and that the material
	 *  it uses is unlit. */
	UStaticMeshComponent* GetPatch() const { return Patch; }

	/** The outermost, near-black ring. Read by tests. */
	UStaticMeshComponent* GetEdge() const { return Edge; }

	/** The bright ring between the two. Read by tests. */
	UStaticMeshComponent* GetRing() const { return Ring; }

	/** The light line just outside the fill. Read by tests. */
	UStaticMeshComponent* GetInner() const { return Inner; }

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
	 * The near-black rim, drawn as the same shape a little larger and a little
	 * lower so the fill sits on top of it.
	 *
	 * A SECOND MESH RATHER THAN A RING IN THE MATERIAL. Drawing the rim in the
	 * material means reading the shape's own texture coordinates, and the two
	 * meshes here map theirs differently -- a cylinder's cap is radial, a cube's
	 * face is not -- so one material could not draw a rim on both without a
	 * branch per shape. A second component costs one draw call and works on
	 * anything.
	 *
	 * NO COLLISION, for the reason the fill has none.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Telegraph")
	TObjectPtr<UStaticMeshComponent> Edge;

	/** The bright ring, inside the near-black one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Telegraph")
	TObjectPtr<UStaticMeshComponent> Ring;

	/** The light line, inside the bright ring and immediately outside the fill.
	 *  It is what keeps the marker readable on mid-grey and on lava, where the
	 *  other two rings are both close to the ground's own brightness. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Telegraph")
	TObjectPtr<UStaticMeshComponent> Inner;

	/**
	 * The unlit material both components are drawn with, found in the
	 * constructor.
	 *
	 * A missing material is not fatal, and is handled the way a missing mesh is:
	 * the marker still spawns, still measures and still goes away on time. It
	 * falls back to the engine default, which is lit -- so it is visible in the
	 * editor and wrong in exactly the way issue #539 describes, rather than
	 * invisible.
	 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	/**
	 * Give one ring its mesh, its size and its height.
	 *
	 * @param BandRadiusCm  the ring's outer radius. What shows of it is the
	 *   difference between this and the next ring in, because each smaller disc
	 *   sits on top of it.
	 * @param StepsDown  how many steps below the fill this sits, so the four
	 *   discs stack in the right order rather than fighting over one depth.
	 */
	void BuildCircleBand(UStaticMeshComponent* Component, float BandRadiusCm,
						 int32 StepsDown);

	/** The same for a lane. @param GrowByCm how far past the danger this ring
	 *  reaches, on both axes. */
	void BuildLaneBand(UStaticMeshComponent* Component, float LaneLengthCm,
					   float HalfWidthCm, float GrowByCm, int32 StepsDown);

	/** Set the colour and opacity on all four bands. Called once per marker,
	 *  after the meshes and scales are set. */
	void ApplyColours();

	/**
	 * Tell the fill's material where the sweep starts, how far it has to go,
	 * when it began and how long it has. Called once, by ApplyColours.
	 *
	 * ON THE FILL AND ON NOTHING ELSE. The three rings say where the danger is
	 * and have to be visible from the first frame; only the interior says how
	 * much time is left. The rings are left on the material's own all-zero
	 * SweepScale, which draws every pixel immediately, and
	 * Cataclysm.Telegraph.OnlyTheFillSweeps checks that default has not moved.
	 */
	void ApplySweep(UMaterialInstanceDynamic* Fill) const;

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
