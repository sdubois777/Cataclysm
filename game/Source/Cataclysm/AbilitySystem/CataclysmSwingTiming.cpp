// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSwingTiming.h"

#include "Animation/AnimSequenceBase.h"

FString UCataclysmSwingConnectsNotify::GetNotifyName_Implementation() const
{
	// WITHOUT THIS THE TIMELINE READS "CataclysmSwingConnects", which is the
	// class name with the U and the Notify suffix trimmed by the editor. The
	// plain words are what somebody scrubbing a clip needs to see.
	return TEXT("Swing Connects");
}

float UCataclysmSwingTiming::MarkedStrikeSeconds(const UAnimSequenceBase* Clip)
{
	if (!Clip)
	{
		return -1.0f;
	}

	// `Notifies` IS PUBLIC IN C++ AND UNREACHABLE FROM PYTHON, which is worth
	// writing down because the opposite is recorded elsewhere in this
	// repository. `tools/measure_attack_impact.py` states that
	// `clip.get_editor_property("notifies")` is refused and that
	// `unreal.AnimationLibrary` is the route that works. That is true of the
	// scripting layer only. From C++ the array is a plain public member of
	// `UAnimSequenceBase`.
	//
	// THE FIRST MARKER, NOT THE LAST. The array is documented as sorted by
	// time, so the first match is the earliest. A clip with two markers is a
	// mistake rather than a design, and taking the earliest makes that mistake
	// land the blow early instead of dropping it.
	for (const FAnimNotifyEvent& Event : Clip->Notifies)
	{
		if (Event.Notify && Event.Notify->IsA<UCataclysmSwingConnectsNotify>())
		{
			// `GetTriggerTime` RATHER THAN THE RAW `DisplayTime`, because a
			// notify carries a trigger offset used to nudge it off an exact
			// frame boundary, and the offset is part of where it fires.
			return Event.GetTriggerTime();
		}
	}

	return -1.0f;
}

float UCataclysmSwingTiming::SecondsUntilSwingConnects(
	const float ClipLengthSeconds,
	const float MarkedSeconds,
	const float PlayRate,
	const float SwingIntervalSeconds)
{
	// NO CLIP MEANS THE BLOW LANDS NOW, and that is the whole of the no-art
	// case. A checkout without the animation assets, and every automation test,
	// reach this line. Returning zero leaves those exactly as they behaved
	// before issue #1133, which is the one thing that must not change.
	if (ClipLengthSeconds <= 0.0f || PlayRate <= 0.0f)
	{
		return 0.0f;
	}

	// AN UNMARKED CLIP FALLS BACK TO THE MIDDLE. Negative is what
	// MarkedStrikeSeconds answers for "no marker"; zero is a real answer meaning
	// the first frame, so the test has to be on the sign and not on falsiness.
	const float StrikeInClip = MarkedSeconds >= 0.0f
		? FMath::Min(MarkedSeconds, ClipLengthSeconds)
		: ClipLengthSeconds * UnmarkedStrikeFraction;

	// THE DIVISION IS THE WHOLE MECHANISM. A marker is a position inside the
	// clip, and the clip's position advances by real time multiplied by the play
	// rate, so the wall-clock wait is the position divided by the rate. The play
	// rate is what already scales with the weapon's attack speed, so the blow
	// tracks attack speed without anything else being told about it.
	const float Wait = StrikeInClip / PlayRate;

	// AND IT IS PULLED BACK INSIDE THE SWING WHEN THE ANIMATION CANNOT KEEP UP.
	// See LatestFractionOfSwingInterval for why. An interval of zero means
	// nothing is driving a rate -- a character holding no weapon -- and there is
	// then no following swing to land in front of.
	if (SwingIntervalSeconds > 0.0f)
	{
		return FMath::Min(Wait, SwingIntervalSeconds * LatestFractionOfSwingInterval);
	}

	return Wait;
}
