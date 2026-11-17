// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyDeath.h"

int32 UCataclysmEnemyDeath::ClipToPlay(int32 ClipCount, FRandomStream& Stream)
{
	// NO CLIPS IS THE ORDINARY CASE, not a fault. Five of the seven vertical
	// slice creatures have no art and die wearing a placeholder cylinder.
	if (ClipCount <= 0)
	{
		return INDEX_NONE;
	}

	// ONE CLIP IS NOT DRAWN FOR, and that is worth doing rather than letting
	// RandRange answer 0 anyway: it leaves the stream untouched, so a creature
	// with one death clip and a creature with none draw the same number of
	// values -- which is zero -- and neither can shift anything drawn after
	// them.
	if (ClipCount == 1)
	{
		return 0;
	}

	// INCLUSIVE AT BOTH ENDS, which is what FRandomStream::RandRange is, so the
	// last clip is reachable. An exclusive upper bound here would silently make
	// the Abyssal Warden always die the same way.
	return Stream.RandRange(0, ClipCount - 1);
}

float UCataclysmEnemyDeath::CorpseSecondsFor(float ClipLength)
{
	// A CLIP THAT IS NOT REALLY A CLIP KEEPS NOTHING. See the header: the honest
	// answer to a missing or broken asset is the behaviour this replaced, which
	// is gone on the next tick.
	if (ClipLength <= 0.0f)
	{
		return 0.0f;
	}

	// THE CLIP'S OWN LENGTH, UP TO THE CEILING. Playing it and then removing the
	// body is the whole rule; the ceiling is a guard against an art choice made
	// in a folder rather than in code.
	return FMath::Min(ClipLength, LongestCorpseSeconds);
}
