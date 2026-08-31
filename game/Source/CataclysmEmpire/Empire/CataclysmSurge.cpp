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

TArray<int32> UCataclysmSurgeScheduler::PickTargets(
	const UCataclysmEmpireMap& Map, int32 Count, FRandomStream& Stream) const
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

	float Total = 0.0f;
	for (const int32 CityId : Exposed)
	{
		const FCataclysmCity* City = Map.Find(CityId);
		const float Weight = City ? TargetWeightFor(City->Tier) : 0.0f;
		if (Weight <= 0.0f)
		{
			continue;
		}

		Candidates.Add(CityId);
		Weights.Add(Weight);
		Total += Weight;
	}

	if (Candidates.IsEmpty() || Total <= 0.0f)
	{
		// EVERY EXPOSED CITY IS WEIGHTED AT NOTHING. The only tier that is, is
		// the Pillar, so this cannot happen on a built map; it is here because
		// dividing by it would be worse than answering nothing.
		return Landed;
	}

	Landed.Reserve(Count);

	for (int32 Rolled = 0; Rolled < Count; ++Rolled)
	{
		// WITH REPLACEMENT. Two dungeons of one wave may land on the same city,
		// which is how a wave concentrates rather than spreading evenly.
		float Roll = Stream.FRand() * Total;

		int32 Chosen = Candidates.Last();
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Roll -= Weights[Index];
			if (Roll <= 0.0f)
			{
				Chosen = Candidates[Index];
				break;
			}
		}

		Landed.Add(Chosen);
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

	// THE TIMER COMES FROM THE DEPTH AND THEN VARIES. `ResolveDaysFor` is the
	// day clock's, so a dungeon's timer and a dungeon's walk are set by the same
	// number; the jitter is what stops two dungeons of one depth coming due on
	// the same day. See `ResolveJitter`.
	const float Base = UCataclysmDayClock::ResolveDaysFor(Dungeon.Floors);
	const float Jitter = 1.0f + Stream.FRandRange(-ResolveJitter, ResolveJitter);

	Dungeon.ResolveDays = Base * Jitter;

	return Dungeon;
}

TArray<FCataclysmDungeon> UCataclysmSurgeScheduler::RollWave(
	const UCataclysmEmpireMap& Map, int32 Day, int32 FirstDungeonId,
	FRandomStream& Stream) const
{
	TArray<FCataclysmDungeon> Wave;

	const TArray<int32> Targets =
		PickTargets(Map, DungeonsInNextSurge(), Stream);

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
