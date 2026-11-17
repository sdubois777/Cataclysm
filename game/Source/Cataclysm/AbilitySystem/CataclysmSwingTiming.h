// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmSwingTiming.generated.h"

class UAnimSequenceBase;

/**
 * The marker an animator puts on the frame where a blow connects.
 *
 * WHY THIS EXISTS. Issue #1133. Until it, damage and every effect fired in the
 * frame the ability activated, while the swing animation played for one to
 * nearly two seconds beside it. An enemy took damage while the weapon was still
 * going backwards.
 *
 * WHAT IT IS FOR: MARKING A POSITION, NOT FIRING AN EVENT. `Notify` below does
 * nothing on purpose. The game reads where this marker SITS on the clip and
 * schedules the blow itself; it does not wait for the marker to be reached.
 * That is a deliberate choice and the reasons are worth stating, because the
 * ordinary Unreal pattern is the other way round.
 *
 *   IT WORKS WITHOUT A TICKING WORLD. A notify only fires when a skeletal mesh
 *   component ticks its animation instance. Every automation test in this
 *   project builds its world with `UWorld::CreateWorld`, which is never ticked,
 *   and the test command passes `-nullrhi`. A design that waited for the marker
 *   would be untestable here and would deal no damage at all under test.
 *
 *   IT WORKS WITH NO ART. A checkout without an animation plays no clip, so no
 *   marker is ever reached. Reading a position lets that case fall back to
 *   zero, which means the blow lands at once, exactly as it did before this
 *   existed, rather than never landing.
 *
 *   IT IS THE SAME NUMBER EITHER WAY. A notify's time is a position inside the
 *   clip, and a montage advances that position by real time multiplied by the
 *   play rate -- see `FAnimMontageInstance::Advance` in the engine. So a marker
 *   reached at wall-clock `position / rate` is exactly what
 *   `SecondsUntilSwingConnects` below computes. Nothing about the scaling with
 *   attack speed is given up by computing it.
 *
 * WHY A CLASS RATHER THAN A BARE NAMED NOTIFY. `AddAnimationNotifyEvent`, which
 * is how these are placed by script, takes a notify class. A class is also
 * greppable and cannot be misspelt on a timeline, which a name can.
 */
UCLASS(meta = (DisplayName = "Cataclysm Swing Connects"))
class CATACLYSM_API UCataclysmSwingConnectsNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	/**
	 * Deliberately empty. See the class comment: this marker is read for its
	 * position rather than waited on. Overridden so that reaching it costs
	 * nothing rather than running the base class's Blueprint dispatch.
	 */
	virtual void Notify(USkeletalMeshComponent* MeshComponent,
						UAnimSequenceBase* Animation,
						const FAnimNotifyEventReference& EventReference) override
	{
	}

	/** What the marker is called on the timeline in the editor. */
	virtual FString GetNotifyName_Implementation() const override;
};

/**
 * When a swing connects, and how that survives being played faster.
 *
 * EVERY JUDGEMENT IS A STATIC FUNCTION HERE, which is the shape
 * `UCataclysmBasicAttack` already uses and for the same stated reason: a world
 * built by `UWorld::CreateWorld` is never ticked, so no automation test can
 * watch a timer fire. What can be tested is every decision the timer makes when
 * it does.
 */
UCLASS()
class CATACLYSM_API UCataclysmSwingTiming : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Where in a clip the blow lands, as a fraction of the clip's own length.
	 *
	 * USED ONLY WHEN THE CLIP CARRIES NO MARKER. Every figure the project has
	 * measured sits between 0.346 and 0.588 -- see the measured table in
	 * `game/docs/player-source-assets.md` -- so the middle of a clip is the
	 * least wrong single answer for a clip nobody has marked yet.
	 *
	 * IT IS A FALLBACK AND NOT A DEFAULT TO RELY ON. A clip with a marker
	 * ignores this entirely.
	 */
	static constexpr float UnmarkedStrikeFraction = 0.5f;

	/**
	 * The latest a blow may land, as a fraction of the interval between swings.
	 *
	 * WHY A CLAMP IS NEEDED AT ALL. `ACataclysmPlayerCharacter` caps the play
	 * rate at 2.5, and attack speed has no design ceiling. Past that cap the
	 * clip takes longer than the interval, so the next swing begins before this
	 * one has finished and the marker for this swing would be reached after the
	 * following swing had already started. `MM_Attack_03` is already exactly at
	 * that cap on the fastest base weapon with no affixes at all.
	 *
	 * LANDING EARLY IS BETTER THAN NOT LANDING. Past the cap the animation
	 * cannot be matched however the timing is done, so the blow is pulled
	 * forward to inside the swing rather than allowed to slide past it.
	 *
	 * THE FIGURE IS A JUDGEMENT AND IS EXPECTED TO BE TUNED BY PLAYING. It is
	 * not derived from anything.
	 */
	static constexpr float LatestFractionOfSwingInterval = 0.9f;

	/**
	 * Read the marker off a clip, in seconds from the clip's start.
	 *
	 * @return  the first `UCataclysmSwingConnectsNotify` on the clip, or a
	 *          negative number when the clip is null or carries no marker.
	 *          Negative rather than zero, because zero is a legitimate answer
	 *          meaning "the blow lands on the first frame".
	 */
	static float MarkedStrikeSeconds(const UAnimSequenceBase* Clip);

	/**
	 * How long from the start of a swing until the blow lands.
	 *
	 * @param ClipLengthSeconds   the clip's authored length
	 * @param MarkedSeconds       from MarkedStrikeSeconds; negative when unmarked
	 * @param PlayRate            what the clip is being played at, 1 is authored
	 * @param SwingIntervalSeconds  the gap between swings, or 0 when nothing
	 *                              is driving a rate
	 *
	 * @return  seconds to wait, never negative. Zero means the blow lands now,
	 *          which is what a caller with no animation at all gets.
	 */
	static float SecondsUntilSwingConnects(float ClipLengthSeconds,
										   float MarkedSeconds,
										   float PlayRate,
										   float SwingIntervalSeconds);
};
