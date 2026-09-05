// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmSurge.h"

#include "DayClock/CataclysmDayClock.h"

// ---------------------------------------------------------------------------
// One dungeon
// ---------------------------------------------------------------------------

float FCataclysmDungeon::BiteScale() const
{
	const FCataclysmDungeonSpec Spec =
		UCataclysmSurgeScheduler::SpecFor(Type, CityTier);

	// A TYPICAL DUNGEON OF THIS KIND ON THIS TIER, which is the midpoint of its
	// floor range. `Simulation._resolve` scales the same way, and it is what
	// makes a deep dungeon hurt more than a shallow one on the same city rather
	// than only taking longer to walk.
	const float Typical = (Spec.LeastFloors + Spec.MostFloors) / 2.0f;

	return Typical <= 0.0f ? 1.0f : Floors / Typical;
}

// ---------------------------------------------------------------------------
// Where a wave lands
// ---------------------------------------------------------------------------

float UCataclysmSurgeScheduler::TargetWeightFor(ECataclysmCityTier Tier)
{
	switch (Tier)
	{
	case ECataclysmCityTier::Bulwark:	return BulwarkTargetWeight;
	case ECataclysmCityTier::Sanctuary:	return SanctuaryTargetWeight;
	case ECataclysmCityTier::Pillar:	return PillarTargetWeight;
	default:							return OutpostTargetWeight;
	}
}

// ---------------------------------------------------------------------------
// What a dungeon is
// ---------------------------------------------------------------------------

FCataclysmDungeonSpec UCataclysmSurgeScheduler::SpecFor(
	ECataclysmDungeonType Type, ECataclysmCityTier Tier)
{
	if (Type != ECataclysmDungeonType::Basic)
	{
		// NOTHING BUILDS THE OTHER THREE, so there is nothing honest to answer.
		// A spec of one floor and no bite is what `IsBuilt` reads as false, and
		// it is deliberately useless rather than a plausible-looking guess that
		// a caller could take for a designed number.
		return FCataclysmDungeonSpec();
	}

	FCataclysmDungeonSpec Spec;

	switch (Tier)
	{
	case ECataclysmCityTier::Bulwark:
		Spec.LeastFloors = 15;
		Spec.MostFloors = 25;
		Spec.DefenceBite = 0.09f;
		Spec.PopulationBite = 0.05f;
		break;

	case ECataclysmCityTier::Sanctuary:
		Spec.LeastFloors = 25;
		Spec.MostFloors = 40;
		Spec.DefenceBite = 0.08f;
		Spec.PopulationBite = 0.04f;
		break;

	case ECataclysmCityTier::Pillar:
		Spec.LeastFloors = 40;
		Spec.MostFloors = 60;
		Spec.DefenceBite = 0.06f;
		Spec.PopulationBite = 0.03f;
		break;

	default:
		Spec.LeastFloors = 8;
		Spec.MostFloors = 15;
		Spec.DefenceBite = 0.10f;
		Spec.PopulationBite = 0.05f;
		break;
	}

	return Spec;
}

// ---------------------------------------------------------------------------
// The schedule
// ---------------------------------------------------------------------------

float UCataclysmSurgeScheduler::DungeonMultiplier() const
{
	// 2 IS HERETIC. See `LethalityRung`; the numbering is `ECataclysmLethality`'s
	// and `tools/tests/test_day_clock_port.py` proves it stays that way.
	return LethalityRung == 2 ? HereticDungeonMultiplier : 1.0f;
}

int32 UCataclysmSurgeScheduler::DungeonsInNextSurge() const
{
	float Count = static_cast<float>(DungeonsPerSurge);

	if (Mode == ECataclysmSurgeMode::Swelling || Mode == ECataclysmSurgeMode::Both)
	{
		Count += CountGrowthPerSurge * SurgeIndex;
	}

	Count = FMath::Min(Count, static_cast<float>(MostDungeonsPerSurge));

	// THE CAP FIRST AND THE LETHALITY MODE SECOND. See
	// `HereticDungeonMultiplier`: the order is what stops Heretic collapsing
	// into Standard at the cap.
	return FMath::Max(1, FMath::TruncToInt(Count * DungeonMultiplier()));
}

float UCataclysmSurgeScheduler::GapAfterThisSurge() const
{
	float Gap = IntervalDays;

	if (Mode == ECataclysmSurgeMode::Accelerating
		|| Mode == ECataclysmSurgeMode::Both)
	{
		Gap *= FMath::Pow(IntervalDecay, static_cast<float>(SurgeIndex));
	}

	return FMath::Max(LeastIntervalDays, Gap);
}

bool UCataclysmSurgeScheduler::IsDue(int32 Day) const
{
	return static_cast<float>(Day) >= NextSurgeDay;
}

float UCataclysmSurgeScheduler::DaysUntilNextSurge(int32 Day) const
{
	return FMath::Max(0.0f, NextSurgeDay - static_cast<float>(Day));
}

void UCataclysmSurgeScheduler::RecordSurge(int32 Day, bool bFromCityFall)
{
	++SurgesFired;

	// THE GAP IS READ BEFORE THE COUNTER MOVES, which is the order
	// `Simulation.trigger_surge` uses. Reading it afterwards would skip the
	// first step of an accelerating run's decay.
	NextSurgeDay = static_cast<float>(Day) + GapAfterThisSurge();

	if (!bFromCityFall || bCityFallAdvancesEscalation)
	{
		++SurgeIndex;
	}
}

// ---------------------------------------------------------------------------
// The wave
// ---------------------------------------------------------------------------

int32 UCataclysmSurgeScheduler::RoomLeftOn(
	const FCataclysmCity& City, const TArray<int32>& DungeonsPerCity)
{
	// NO CAP MEANS NO LIMIT. A city that never bought the upgrade carries zero
	// for the effect, and zero here is "no cap" rather than "no room" -- reading
	// it the other way would stop every wave landing anywhere at all.
	const float Cap =
		City.UpgradeValueFor(ECataclysmCityUpgradeEffect::DungeonCap);

	if (Cap <= 0.0f)
	{
		return MAX_int32;
	}

	// AND NEITHER DOES A CALLER THAT DID NOT SAY. `UCataclysmEmpireRun` owns the
	// dungeons; this class deliberately does not, and a test that rolls a wave
	// straight off a map has none to count.
	if (!DungeonsPerCity.IsValidIndex(City.CityId))
	{
		return MAX_int32;
	}

	return FMath::Max(0, FMath::RoundToInt(Cap) - DungeonsPerCity[City.CityId]);
}

TArray<int32> UCataclysmSurgeScheduler::PickTargets(
	const UCataclysmEmpireMap& Map, int32 Count, FRandomStream& Stream,
	const TArray<int32>& DungeonsPerCity) const
{
	TArray<int32> Landed;

	// THE FRONTIER, AND THE PILLAR IS NOT IN IT. `ExposedCities` leaves the
	// Pillar out unless asked for, which is the same rule
	// `PillarTargetWeight` states from the other side. Both are here because
	// either alone would be a single point of failure for a rule the design
	// states plainly.
	const TArray<int32> Exposed = Map.ExposedCities();
	if (Exposed.IsEmpty() || Count <= 0)
	{
		return Landed;
	}

	// A CITY WEIGHTED AT NOTHING IS LEFT OUT RATHER THAN LEFT IN WITH A ZERO.
	// A cumulative walk over a list holding zeroes can land on one of them, when
	// the roll happens to arrive at a boundary exactly, and a tier weighted at
	// nothing means "never", not "rarely".
	TArray<int32> Candidates;
	TArray<float> Weights;

	// HOW MANY MORE EACH CANDIDATE WILL ACCEPT, counted down as the wave is
	// built. A city with no dungeon cap carries `MAX_int32` and is never
	// removed, which is what makes a run with no caps roll exactly what it
	// rolled before this existed.
	TArray<int32> Room;

	float Total = 0.0f;
	for (const int32 CityId : Exposed)
	{
		const FCataclysmCity* City = Map.Find(CityId);
		const float Weight = City ? TargetWeightFor(City->Tier) : 0.0f;
		if (Weight <= 0.0f)
		{
			continue;
		}

		// A CITY ALREADY AT ITS CAP IS NOT A CANDIDATE AT ALL.
		const int32 Left = RoomLeftOn(*City, DungeonsPerCity);
		if (Left <= 0)
		{
			continue;
		}

		Candidates.Add(CityId);
		Weights.Add(Weight);
		Room.Add(Left);
		Total += Weight;
	}

	if (Candidates.IsEmpty() || Total <= 0.0f)
	{
		// NOTHING LEFT TO LAND ON. Either every exposed city is weighted at
		// nothing -- only the Pillar is, so that cannot happen on a built map --
		// or every one of them has reached its dungeon cap, which can.
		return Landed;
	}

	Landed.Reserve(Count);

	for (int32 Rolled = 0; Rolled < Count; ++Rolled)
	{
		// WITH REPLACEMENT. Two dungeons of one wave may land on the same city,
		// which is how a wave concentrates rather than spreading evenly.
		float Roll = Stream.FRand() * Total;

		int32 Chosen = Candidates.Num() - 1;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Roll -= Weights[Index];
			if (Roll <= 0.0f)
			{
				Chosen = Index;
				break;
			}
		}

		Landed.Add(Candidates[Chosen]);

		// AND THAT CITY HAS ONE LESS ROOM. A city one short of its cap can be
		// chosen twice in one wave, because the roll is with replacement, so
		// counting down only when the wave is finished would let it go over.
		//
		// A CITY WITH NO CAP COUNTS DOWN FROM `MAX_int32` AND NEVER REACHES
		// ZERO, so nothing below runs for a run that bought no caps and the
		// draws stay identical to what they were.
		if (--Room[Chosen] > 0)
		{
			continue;
		}

		// FULL, SO IT STOPS BEING A CANDIDATE AND THE WAVE LANDS ELSEWHERE.
		// `Total` has to lose its weight with it or every later roll would be
		// scaled against a total that includes a city it can no longer pick, and
		// the walk would fall off the end onto whichever city happens to be last.
		Total -= Weights[Chosen];

		Candidates.RemoveAt(Chosen);
		Weights.RemoveAt(Chosen);
		Room.RemoveAt(Chosen);

		if (Candidates.IsEmpty() || Total <= 0.0f)
		{
			// EVERY EXPOSED CITY IS FULL. The rest of the wave has nowhere to go
			// and the wave is short, which is the honest answer rather than
			// putting dungeons somewhere the design forbids.
			break;
		}
	}

	return Landed;
}

FCataclysmDungeon UCataclysmSurgeScheduler::MakeDungeon(
	int32 DungeonId, const FCataclysmCity& City, int32 Day,
	FRandomStream& Stream) const
{
	const FCataclysmDungeonSpec Spec =
		SpecFor(ECataclysmDungeonType::Basic, City.Tier);

	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = DungeonId;
	Dungeon.Type = ECataclysmDungeonType::Basic;
	Dungeon.CityId = City.CityId;
	Dungeon.CityTier = City.Tier;
	Dungeon.SpawnedDay = Day;
	Dungeon.DefenceBite = Spec.DefenceBite;
	Dungeon.PopulationBite = Spec.PopulationBite;

	// INCLUSIVE AT BOTH ENDS, which is what `FRandomStream::RandRange` is and
	// what `random.randint` is, so the deepest dungeon the table describes can
	// actually be rolled.
	Dungeon.Floors = FMath::Max(1, Stream.RandRange(Spec.LeastFloors,
													Spec.MostFloors));

	// AND THEN THE CITY'S OWN UPGRADES MOVE IT. Both are read and the difference
	// applied once, so a city holding both ends up with the net change rather
	// than with whichever happened to be checked second.
	//
	// AFTER THE ROLL AND NOT INSTEAD OF IT, so the depth a dungeon would have
	// had is still drawn from the same range with the same draw. Rolling a
	// different range would change every later roll in the run.
	const int32 Deeper = FMath::RoundToInt(
		City.UpgradeValueFor(ECataclysmCityUpgradeEffect::DungeonFloorsMore));

	const int32 Shallower = FMath::RoundToInt(
		City.UpgradeValueFor(ECataclysmCityUpgradeEffect::DungeonFloorsFewer));

	// A MINIMUM OF ONE, which is what "to a minimum of 1" in the sheet says and
	// what a dungeon with no floors would otherwise be: unwalkable.
	Dungeon.Floors = FMath::Max(1, Dungeon.Floors + Deeper - Shallower);

	// THE TIMER COMES FROM THE DEPTH AND THEN VARIES. `ResolveDaysFor` is the
	// day clock's, so a dungeon's timer and a dungeon's walk are set by the same
	// number; the jitter is what stops two dungeons of one depth coming due on
	// the same day. See `ResolveJitter`.
	//
	// SO THE FLOOR UPGRADES MOVE THE TIMER TOO, and that is the point rather
	// than a side effect. One floor costs exactly one day, which
	// `docs/Cataclysm_GDD_v2.md` states and `CLAUDE.md` fixes: a deeper dungeon
	// is slower to walk, worth more, and slower to bite, and a shallower one is
	// quicker, poorer, and bites sooner. Adjusting the timer separately would
	// break the one rule the whole strategy layer rests on.
	const float Base = UCataclysmDayClock::ResolveDaysFor(Dungeon.Floors);
	const float Jitter = 1.0f + Stream.FRandRange(-ResolveJitter, ResolveJitter);

	Dungeon.ResolveDays = Base * Jitter;

	return Dungeon;
}

TArray<FCataclysmDungeon> UCataclysmSurgeScheduler::RollWave(
	const UCataclysmEmpireMap& Map, int32 Day, int32 FirstDungeonId,
	FRandomStream& Stream, const TArray<int32>& DungeonsPerCity) const
{
	TArray<FCataclysmDungeon> Wave;

	const TArray<int32> Targets =
		PickTargets(Map, DungeonsInNextSurge(), Stream, DungeonsPerCity);

	Wave.Reserve(Targets.Num());

	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		const FCataclysmCity* City = Map.Find(Targets[Index]);
		if (City == nullptr)
		{
			continue;
		}

		Wave.Add(MakeDungeon(FirstDungeonId + Index, *City, Day, Stream));
	}

	return Wave;
}
