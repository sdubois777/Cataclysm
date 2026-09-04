// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmGroundEffect.generated.h"

/**
 * What a patch of burning ground looks like.
 *
 * WHAT THIS FILLS, AND IT DREW NOTHING WHATSOEVER. `ACataclysmGroundZone`'s own
 * header said so plainly: "NO MESH AND NO PARTICLE EFFECT. This project has no
 * art content at all... so the zone is invisible and its effect is the only
 * evidence it is there." That was written before any effect existed and it has
 * been true ever since. Eight of the sixteen designed Demonic skills leave one
 * of these -- Molten Cleave drags a line of molten slag, Infernal Plunge leaves
 * a pool of lava, Pyroclasm sets the ground burning -- and a player could stand
 * in any of them and see nothing at all.
 *
 * IT IS ALSO WHAT A STOMP IS. The project owner said on 2026-08-22 that "the
 * big stomps and ring aoes are still nothing". A round zone drawn with an
 * expanding ring at its edge is the stomp; the ring is `NS_Impact_Ground`'s
 * primary shape and it stops exactly where the damage stops. Issue #811.
 *
 * A ZONE IS A CAPSULE AND NOT ALWAYS A CIRCLE, which is why this takes two
 * ends. `ACataclysmGroundZone::IsLong` is true when they differ, and a long one
 * is drawn as a row of copies along the segment rather than as one circle at
 * the middle. A circle at the middle of Emberhurl's twelve metre flight path
 * would tell the player the safe ground is dangerous and the dangerous ground is
 * safe, which is worse than drawing nothing.
 */
UCLASS()
class CATACLYSM_API UCataclysmGroundEffect : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The system built for this, answering issue #811. */
	static const TCHAR* SystemAssetPath;

	/**
	 * The user parameters this class writes. Bare names without the `User.`
	 * prefix, which Niagara adds itself, and taken from
	 * CataclysmEffectParameterNames so there is one spelling of each in the
	 * whole project.
	 *
	 * A PARAMETER THE SYSTEM DOES NOT EXPOSE IS IGNORED IN SILENCE -- no
	 * warning, no error, no effect.
	 * `Cataclysm.Effects.GroundZoneExposesTheStandardParameterBlock` is what
	 * notices.
	 */
	static const FName ElementColourParameter;
	static const FName ElementColourDarkParameter;
	static const FName ScaleParameter;
	static const FName DurationParameter;

	/**
	 * What `User.Scale` is set to for a zone of this radius.
	 *
	 * THE RING MESH REACHES ONE METRE AT SCALE ONE, measured from its own
	 * bounds: `SM_VFX_Cyl_In_Out_Floor_01` is a flat disc 200 centimetres
	 * across. So dividing the radius by 100 makes the drawn ring stop exactly
	 * where the damage stops, which is the whole job of this effect -- a player
	 * decides where to stand by looking at it.
	 *
	 * CLAMPED AT BOTH ENDS, AND THE BOUNDS ARE GUARDS RATHER THAN DESIGN. The
	 * designed ground zones run from 3 metres to 7, so neither bound is a number
	 * anything designed asks for.
	 *
	 * SEPARATE AND STATIC SO A TEST CAN REACH IT, because issue #559 records
	 * that no test here can observe the spawn itself.
	 */
	static float ScaleFor(float RadiusCm);

	/** The smallest and largest `User.Scale` a zone may ask for. */
	static constexpr float MinimumScale = 0.5f;
	static constexpr float MaximumScale = 12.0f;

	/**
	 * How many copies cover a zone of this length end to end.
	 *
	 * ONE FOR A ROUND ZONE. A length of zero is a circle and needs a single
	 * copy at its centre.
	 *
	 * ENOUGH THAT NEIGHBOURS OVERLAP FOR A LONG ONE. Copies are spread evenly
	 * from one end to the other, so `Count - 1` gaps span the length and each
	 * gap must be no wider than the diameter or the drawn patch has holes in it
	 * that still deal damage.
	 *
	 * CAPPED, AND THE CAP IS A COST DECISION RATHER THAN A DESIGN ONE. Each copy
	 * is a Niagara component with a mesh renderer, and nothing has measured what
	 * they cost -- issue #547. `MostCopies` is what stops a very long, very
	 * narrow zone from asking for dozens. A zone that hits the cap is drawn with
	 * gaps, which is why the cap is high enough that no designed zone reaches
	 * it: the longest is Infernal Lance's twelve metre line at 2 metres wide,
	 * which asks for four.
	 */
	static int32 HowManyAlong(float LengthCm, float RadiusCm);

	/** The most copies one zone may draw. */
	static constexpr int32 MostCopies = 8;

	/**
	 * How many times a zone has asked to be drawn since the editor started.
	 *
	 * IT EXISTS BECAUSE NO TEST IN THIS PROJECT CAN SEE A NIAGARA SPAWN. The
	 * automation command passes `-nullrhi` and Niagara refuses to create a
	 * component when `FApp::CanEverRender()` is false, so `PlayFor` can spawn
	 * nothing and there is nothing to assert about. Issue #559.
	 *
	 * WITHOUT IT THE WHOLE THING COULD BE DELETED FROM THE CALL CHAIN AND THE
	 * SUITE WOULD STAY GREEN. `Cataclysm.Effects.EveryGroundZoneAsksToBeDrawn`
	 * spawns a real zone and reads this.
	 */
	static int32 TimesAsked;

	/**
	 * What the last ask was given, recorded beside the counter above.
	 *
	 * A COUNTER CANNOT TELL A DRAWN ZONE FROM ONE ASKED FOR WITH EVERY NUMBER
	 * ZEROED, and issue #1153 was exactly that: every patch of burning ground in
	 * the game asked to be drawn with a radius of zero, a far end at the world
	 * origin and no duration, because `ACataclysmGroundZone::SpawnAlong` set
	 * those properties on the lines after `SpawnActor`, which runs `BeginPlay`
	 * before it returns. `TimesAsked` went up every time and no test could see
	 * anything wrong.
	 *
	 * FOR THE SAME REASON `TimesAsked` EXISTS. Under `-nullrhi` no Niagara
	 * component is created, so what the caller asked for is the only thing left
	 * to look at. Issue #559.
	 *
	 * `Cataclysm.Effects.AGroundZoneIsDrawnWithItsOwnSizeAndDuration` reads
	 * these. They are for tests and nothing in the game reads them.
	 */
	static FVector LastStart;
	static FVector LastFarEnd;
	static float LastRadiusCm;
	static float LastDuration;

	/**
	 * Draws a zone.
	 *
	 * @param WorldContextObject anything with a world. The zone itself, normally.
	 * @param Start        the near end, which is the zone actor's own location.
	 * @param FarEnd       the far end. Equal to Start for a round zone.
	 * @param RadiusCm     the radius when round, the half width when long.
	 * @param Duration     how many seconds the zone burns for.
	 * @param DamageType   the leaf of the owner's `Element.*` tag, or NAME_None.
	 *
	 * Returns how many components were spawned, which is zero in every
	 * automation test and whenever the effect type's scalability refuses them.
	 *
	 * THE COMPONENTS ARE NOT ATTACHED TO THE ZONE and they clean themselves up.
	 * A ground zone ends by `SetLifeSpan` destroying the actor, and a component
	 * attached to it would be destroyed at the same instant -- the fire would
	 * vanish rather than burn out. They are spawned unattached with
	 * `bAutoDestroy` so each finishes its own life.
	 */
	static int32 PlayFor(const UObject* WorldContextObject, const FVector& Start,
						 const FVector& FarEnd, float RadiusCm, float Duration,
						 FName DamageType);
};
