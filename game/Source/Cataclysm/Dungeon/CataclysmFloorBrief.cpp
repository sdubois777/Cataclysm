// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorBrief.h"

#include "Character/CataclysmEnemyModifiers.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Math/RandomStream.h"

const TCHAR* FCataclysmDungeonFloorRules::UnstableDimensionsKey =
	TEXT("Chaos_Unstable_Dimensions");

const TCHAR* FCataclysmDungeonFloorRules::RealityTwisterKey =
	TEXT("Chaos_Reality_Twister");

const TCHAR* FCataclysmDungeonFloorRules::RuleOfChaosKey =
	TEXT("Chaos_Rule_of_Chaos");

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
	TArray<FName>& OutModifiers, float& OutScore, FName* OutTwistedIn, FName* OutEveryCreatureModifier,
	int32* OutRuleOfChaosChange)
{
	// RULE 1. An ordinary dungeon's floor carries the dungeon's own modifiers,
	// and so does every floor of a dungeon whose pools were never filled.
	OutModifiers = Dungeon.Modifiers;
	OutScore = Dungeon.ModifierScore;
	if (OutTwistedIn)
	{
		*OutTwistedIn = NAME_None;
	}
	if (OutEveryCreatureModifier)
	{
		*OutEveryCreatureModifier = NAME_None;
	}
	if (OutRuleOfChaosChange)
	{
		*OutRuleOfChaosChange = RuleOfChaosNoChange;
	}

	FRandomStream Stream =
		CataclysmFloorBriefStreamFor(Dungeon.DungeonSeed, FloorNumber);

	// RULES 2 AND 4 DRAW DUNGEON ROWS, SO A DUNGEON WHOSE POOLS WERE NEVER FILLED SKIPS THEM; rule 3 draws an enemy
	// modifier from its own table and still runs. Until 2026-10-01 this was a `return`, when rule 3 drew a dungeon row
	// too.
	const bool bAnyDungeonPool = Dungeon.ModifierPool.Num() > 0 || Dungeon.EveryBuiltModifier.Num() > 0;

	// RULE 2. "Dungeon modifiers change every floor." The same number of them a
	// dungeon of this tier and sub-type carries, drawn again for this floor.
	if (bAnyDungeonPool && Dungeon.SubType == ECataclysmDungeonSubType::Volatile)
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

	// RULE 3, UNSTABLE DIMENSIONS, IS DRAWN LAST, BELOW, since 2026-10-01.

	// RULE 4. REALITY TWISTER, AS THE OWNER DECIDED ON 2026-09-26: "Each floor, one
	// random dungeon modifier from any Cataclysm is added, even one this dungeon
	// could not otherwise draw. It is replaced on the next floor."
	//
	// FROM EVERY ROW THAT DOES SOMETHING IN PLAY, not the dungeon's own pool, so
	// a Cataclysm this run is not facing can be drawn. NEVER REALITY TWISTER ITSELF
	// AND NEVER A ROW ALREADY IN FORCE, including rule 3's extra: this floor
	// carries Reality Twister, so leaving out what it carries leaves out both.
	// AFTER RULE 2 AND READING ITS RESULT, so a Volatile floor that drew this row is
	// twisted and one that did not is not.
	// ON THE SAME STREAM, so a floor always draws the same row and the next floor
	// draws again. ITS DANGER IS ADDED, so it counts toward this floor's creatures.
	if (bAnyDungeonPool && OutModifiers.Contains(FName(RealityTwisterKey)))
	{
		const TArray<FCataclysmDungeonModifier> Left =
			CataclysmFloorBriefPoolWithout(Dungeon.EveryBuiltModifier, OutModifiers);

		const TArray<FCataclysmDungeonModifier> Twisted =
			UCataclysmDungeonModifierRules::Draw(Left, 1, Stream);

		if (Twisted.Num() > 0)
		{
			OutModifiers.Append(UCataclysmDungeonModifierRules::KeysOf(Twisted));
			OutScore += UCataclysmDungeonModifierRules::DangerOf(Twisted);
			if (OutTwistedIn)
			{
				*OutTwistedIn = Twisted[0].RowKey;
			}
		}
	}

	// RULE 5. RULE OF CHAOS DRAWS ONE OF ITS THREE RULE CHANGES FOR THIS FLOOR. Issues #1820 and #41. The three were
	// approved by the owner on 2026-10-08 "for now"; see `RuleOfChaosKey`.
	// - FROM THE FLOOR'S FINAL LIST, after rules 2 and 4, so a Volatile floor that re-drew the row and a floor Reality
	//   Twister added it to both draw a change, and a floor without the row draws none.
	// - ON THE FLOOR'S OWN STREAM, as rule 4 draws, so the same dungeon seed and floor give the same change.
	// - EVEN BETWEEN THE THREE: one whole number from 1 to `RuleOfChaosChanges`.
	// - BEFORE RULE 3 AND WHETHER OR NOT THE CALLER ASKED FOR THE ANSWER, so the draw never depends on which pointers a
	//   caller passed. Rule 3 draws only when its pointer is given; a draw placed after it would move with that.
	//   ON A FLOOR CARRYING BOTH ROWS, rule 3's reality is therefore the stream's next draw after this one, and not
	//   the draw it was before this rule existed.
	// - NOT A DUNGEON ROW, so a dungeon whose pools were never filled still draws, as rule 3 does.
	// - IT ADDS NOTHING TO THE SCORE: the row's own danger is already in it.
	if (OutModifiers.Contains(FName(RuleOfChaosKey)))
	{
		const int32 ChangeDrawn = Stream.RandRange(1, RuleOfChaosChanges);
		if (OutRuleOfChaosChange)
		{
			*OutRuleOfChaosChange = ChangeDrawn;
		}
	}

	// RULE 3. "A new 'reality' is imposed, granting a new, random modifier to all enemies on the next floor." Corrected
	// 2026-10-01, as ruled under the owner's delegation:
	// - AN ENEMY MODIFIER, FROM THE GENERIC COLUMN ONLY: ten rows, all with behaviour, the same meaning on any creature.
	//   Given to every creature the floor places, by `ACataclysmDungeonGameMode::SpawnPlacedCreature`.
	// - NONE ON FLOOR 1, where no floor has been cleared; one on every floor from 2, a Horde wave included.
	// - DRAWN LAST, from the floor's final list, so an Unstable Dimensions that Reality Twister added imposes a reality
	//   too: the old judgement that it "adds nothing on that floor" held only while this rule added a dungeon row before
	//   rule 4. On the floor's own stream, so a floor always draws the same reality.
	// - IT ADDS NOTHING TO THE SCORE: a stronger creature, not more of them.
	if (OutEveryCreatureModifier && FloorNumber > 1 && OutModifiers.Contains(FName(UnstableDimensionsKey)))
	{
		const TArray<FName> Reality = UCataclysmEnemyModifiers::Draw(
			UCataclysmEnemyModifiers::LoadEnemyModifierTable(), FName(TEXT("Generic")), 1, Stream, TArray<FName>());
		if (Reality.Num() > 0)
		{
			*OutEveryCreatureModifier = Reality[0];
		}
	}
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
	ModifiersFor(Dungeon, Floor, Brief.Modifiers, Brief.ModifierScore, &Brief.TwistedIn,
				 &Brief.EveryCreatureModifier, &Brief.RuleOfChaosChange);

	return Brief;
}
