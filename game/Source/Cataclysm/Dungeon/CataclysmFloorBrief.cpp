// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorBrief.h"

#include "Dungeon/CataclysmFloorGenerator.h"
#include "Math/RandomStream.h"

const TCHAR* FCataclysmDungeonFloorRules::UnstableDimensionsKey =
	TEXT("Chaos_Unstable_Dimensions");

namespace
{
	/**
	 * The stream a floor's own modifier draws come from.
	 *
	 * NAMED FOR THIS FILE even though it sits in an anonymous namespace, because
	 * Unreal merges a module's `.cpp` files into one translation unit and a
	 * unity build merges those namespaces too.
	 * `tools/tests/test_no_two_files_share_an_anonymous_helper.py` is what fails
	 * when two files declare the same helper.
	 */
	FRandomStream CataclysmFloorBriefStreamFor(int32 DungeonSeed, int32 FloorNumber)
	{
		const int32 FloorSeed =
			FCataclysmFloorGenerator::SeedForFloor(DungeonSeed, FloorNumber);
		return FRandomStream(FCataclysmFloorGenerator::SeedForFloor(
			FloorSeed, FCataclysmDungeonFloorRules::ModifierSalt));
	}

	/** The pool with everything these row keys name taken out of it. */
	TArray<FCataclysmDungeonModifier> CataclysmFloorBriefPoolWithout(
		const TArray<FCataclysmDungeonModifier>& Pool,
		const TArray<FName>& Already)
	{
		TArray<FCataclysmDungeonModifier> Left;
		Left.Reserve(Pool.Num());
		for (const FCataclysmDungeonModifier& Modifier : Pool)
		{
			if (!Already.Contains(Modifier.RowKey))
			{
				Left.Add(Modifier);
			}
		}
		return Left;
	}
}

// ---------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------

ECataclysmFloorLayout FCataclysmDungeonFloorRules::LayoutFor(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	// THE FLOOR NUMBER IS TAKEN AND NOT USED, and that is deliberate rather
	// than an oversight. Every rule in this file has the same signature so that
	// a rule which later does depend on the depth -- a Horde dungeon whose last
	// wave is fought somewhere else, say -- is a change inside one function
	// rather than a change to how the seam is called.
	(void)FloorNumber;

	if (Dungeon.SubType == ECataclysmDungeonSubType::Horde)
	{
		return ECataclysmFloorLayout::Arena;
	}

	return Dungeon.Layout;
}

bool FCataclysmDungeonFloorRules::BossAtTheExit(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	if (Dungeon.SubType == ECataclysmDungeonSubType::Elite)
	{
		return true;
	}

	// A DUNGEON WITH ONE FLOOR OR FEWER HAS NO BOTTOM TO PUT A BOSS ON. That is
	// what `ACataclysmDungeonGameMode::IsOnTheLastFloor` says about the same
	// case: no floor count means no bottom, and descending goes on for ever.
	return Dungeon.TotalFloors > 1 && FloorNumber >= Dungeon.TotalFloors;
}

bool FCataclysmDungeonFloorRules::OneWave(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	(void)FloorNumber;
	return Dungeon.SubType == ECataclysmDungeonSubType::Horde;
}

bool FCataclysmDungeonFloorRules::WaveWalksIn(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	(void)FloorNumber;

	// EVERY WAVE OF A HORDE DUNGEON WALKS IN, THE FIRST INCLUDED. The owner's
	// rule names no exception, and a first wave that stood waiting while every
	// later one arrived would read as two different dungeons.
	return Dungeon.SubType == ECataclysmDungeonSubType::Horde;
}

bool FCataclysmDungeonFloorRules::SameArenaAsLastFloor(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	// FLOOR 1 CARVES THE ARENA AND THE REST ARE WAVES INTO IT. There is no
	// floor before the first for it to be the same space as, so the rule is
	// false there however deep the dungeon is.
	return Dungeon.SubType == ECataclysmDungeonSubType::Horde && FloorNumber > 1;
}

int32 FCataclysmDungeonFloorRules::CarvedAsFloorNumber(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	// EVERY FLOOR OF A HORDE DUNGEON IS FLOOR 1'S ARENA. The floor number still
	// decides the modifiers, the wave and the day spent; it decides nothing
	// about the shape of the space, because there is only one space.
	if (Dungeon.SubType == ECataclysmDungeonSubType::Horde)
	{
		return 1;
	}

	return FloorNumber;
}

float FCataclysmDungeonFloorRules::SightRadiusMultiplierFor(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	(void)FloorNumber;

	if (Dungeon.SubType == ECataclysmDungeonSubType::Horde)
	{
		return HordeSightRadiusMultiplier;
	}

	// ONE AND NOT ZERO. A multiplier of zero would mean a creature that notices
	// nothing at all, which is what every other dungeon would get if this
	// answered with a default-constructed float.
	return 1.0f;
}

int32 FCataclysmDungeonFloorRules::NextWaveArrivesAtOrBelow(int32 WaveSpawned)
{
	if (WaveSpawned <= 0)
	{
		return 0;
	}

	// FLOORED, WHICH IS WHAT "10% OR LESS REMAINING" MEANS. See the header: for
	// seven creatures a tenth is 0.7, one survivor is 14.3% of the wave, and
	// 14.3% is not 10% or less -- so the answer is zero and the wave has to be
	// finished off. `FloorToInt` on a non-negative number is the same as
	// truncation, and the guard above is what keeps it non-negative.
	const int32 Threshold = FMath::FloorToInt(
		static_cast<float>(WaveSpawned) * NextWaveAtFractionRemaining);

	// NEVER MORE THAN THE WAVE ITSELF. A fraction above 1 would otherwise mean
	// a wave that is finished the moment it arrives, and every wave of the
	// dungeon would cascade in one frame.
	return FMath::Clamp(Threshold, 0, WaveSpawned);
}

void FCataclysmDungeonFloorRules::ModifiersFor(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber,
	TArray<FName>& OutModifiers, float& OutScore)
{
	// RULE 1. An ordinary dungeon's floor carries the dungeon's own modifiers,
	// and so does every floor of a dungeon whose pool was never filled.
	OutModifiers = Dungeon.Modifiers;
	OutScore = Dungeon.ModifierScore;

	if (Dungeon.ModifierPool.Num() == 0)
	{
		return;
	}

	FRandomStream Stream =
		CataclysmFloorBriefStreamFor(Dungeon.DungeonSeed, FloorNumber);

	// RULE 2. "Dungeon modifiers change every floor." The same number of them a
	// dungeon of this tier and sub-type carries, drawn again for this floor.
	if (Dungeon.SubType == ECataclysmDungeonSubType::Volatile)
	{
		const int32 Count = UCataclysmDungeonModifierRules::CountFor(
			Dungeon.DifficultyTier, Dungeon.SubType);

		const TArray<FCataclysmDungeonModifier> Drawn =
			UCataclysmDungeonModifierRules::Draw(
				Dungeon.ModifierPool, Count, Stream);

		// A POOL THAT GAVE NOTHING LEAVES THE DUNGEON'S OWN LIST STANDING. It
		// can only happen when the count is zero or the pool is empty, and a
		// floor with no modifiers at all is a worse answer than the dungeon's.
		if (Drawn.Num() > 0)
		{
			OutModifiers = UCataclysmDungeonModifierRules::KeysOf(Drawn);
			OutScore = UCataclysmDungeonModifierRules::DangerOf(Drawn);
		}
	}

	// RULE 3. "A new 'reality' is imposed, granting a new, random modifier to
	// all enemies on the next floor."
	//
	// AFTER RULE 2 AND READING ITS RESULT, so a Volatile dungeon gets the extra
	// on the floors whose re-draw actually landed Unstable Dimensions, and not
	// on the ones it did not. The two rules share a field and neither is
	// written in terms of the other.
	if (!OutModifiers.Contains(FName(UnstableDimensionsKey)))
	{
		return;
	}

	// NOT ONE THIS FLOOR ALREADY CARRIES. Two copies of one environmental
	// effect read to a player as one effect that is worse, which is the
	// reasoning `UCataclysmDungeonModifierRules::Draw` records for the same
	// rule inside a single draw.
	const TArray<FCataclysmDungeonModifier> Left =
		CataclysmFloorBriefPoolWithout(Dungeon.ModifierPool, OutModifiers);

	const TArray<FCataclysmDungeonModifier> Extra =
		UCataclysmDungeonModifierRules::Draw(Left, 1, Stream);

	if (Extra.Num() == 0)
	{
		return;
	}

	OutModifiers.Append(UCataclysmDungeonModifierRules::KeysOf(Extra));
	OutScore += UCataclysmDungeonModifierRules::DangerOf(Extra);
}

// ---------------------------------------------------------------------------
// The whole answer
// ---------------------------------------------------------------------------

FCataclysmFloorBrief FCataclysmDungeonFloorRules::BriefFor(
	const FCataclysmDungeonIdentity& Dungeon, int32 FloorNumber)
{
	const int32 Floor = FMath::Max(1, FloorNumber);

	FCataclysmFloorBrief Brief;
	Brief.FloorNumber = Floor;
	Brief.Layout = LayoutFor(Dungeon, Floor);
	Brief.bBossAtTheExit = BossAtTheExit(Dungeon, Floor);
	Brief.bOneWave = OneWave(Dungeon, Floor);
	Brief.bWaveWalksIn = WaveWalksIn(Dungeon, Floor);
	Brief.bSameArenaAsLastFloor = SameArenaAsLastFloor(Dungeon, Floor);
	Brief.CarvedAsFloorNumber = CarvedAsFloorNumber(Dungeon, Floor);
	Brief.SightRadiusMultiplier = SightRadiusMultiplierFor(Dungeon, Floor);
	ModifiersFor(Dungeon, Floor, Brief.Modifiers, Brief.ModifierScore);

	return Brief;
}
