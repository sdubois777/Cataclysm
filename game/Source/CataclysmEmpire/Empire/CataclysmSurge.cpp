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

float FCataclysmDungeon::WalkDaysPerFloor() const
{
	const int32 Walked = FMath::Max(1, Floors);

	// NOBODY SET IT, SO A FLOOR COSTS THE ORDINARY DAY. A dungeon built by hand
	// in a test, or one from a save written before this field existed, behaves
	// exactly as it did before.
	if (WalkDays <= 0.0f)
	{
		return UCataclysmDayClock::DaysPerFloor;
	}

	return WalkDays / Walked;
}

// ---------------------------------------------------------------------------
// What a dungeon does differently
// ---------------------------------------------------------------------------

float UCataclysmSurgeScheduler::SpawnWeightFor(ECataclysmDungeonSubType SubType)
{
	// LISTED RATHER THAN DEFAULTED. Adding a sub-type to the enum without giving
	// it a weight is then a compiler warning, and not a value that silently never
	// spawns.
	switch (SubType)
	{
	// **NOT A WEIGHT OF ZERO STANDING IN FOR A REAL ONE.** There is no
	// `SpawnWeightNone` constant any more: the owner ruled on 2026-09-05 that
	// every dungeon a surge makes has a sub-type, so no roll can produce this
	// value and zero is the true answer to how often it is rolled. The enum
	// value itself is still needed, because a dungeon entered outside the empire
	// has no sub-type -- `UCataclysmGameMode::RunDungeonSubType` returns this,
	// and `UCataclysmEnemyScore::SubTypeWeight` gives it no difficulty.
	//
	// NOTHING A SURGE MAKES CARRIES IT. A Siege refused by the one-per-city cap
	// used to become one; since 2026-09-06 it is spread across the other six
	// instead. See `SiegesPerCity`.
	case ECataclysmDungeonSubType::None:			return 0.0f;
	case ECataclysmDungeonSubType::Timed:			return SpawnWeightTimed;
	case ECataclysmDungeonSubType::Horde:			return SpawnWeightHorde;
	case ECataclysmDungeonSubType::Siege:			return SpawnWeightSiege;
	case ECataclysmDungeonSubType::CowLevel:		return SpawnWeightCowLevel;
	case ECataclysmDungeonSubType::Elite:			return SpawnWeightElite;
	case ECataclysmDungeonSubType::Volatile:		return SpawnWeightVolatile;
	case ECataclysmDungeonSubType::Sacrificial:		return SpawnWeightSacrificial;
	}

	return 0.0f;
}

FString UCataclysmSurgeScheduler::SubTypeName(ECataclysmDungeonSubType SubType)
{
	switch (SubType)
	{
	case ECataclysmDungeonSubType::Timed:		return TEXT("Timed");
	case ECataclysmDungeonSubType::Horde:		return TEXT("Horde");
	case ECataclysmDungeonSubType::Siege:		return TEXT("Siege");
	case ECataclysmDungeonSubType::CowLevel:	return TEXT("Cow Level");
	case ECataclysmDungeonSubType::Elite:		return TEXT("Elite");
	case ECataclysmDungeonSubType::Volatile:	return TEXT("Volatile");
	case ECataclysmDungeonSubType::Sacrificial:	return TEXT("Sacrificial");
	default:									return FString();
	}
}

float UCataclysmSurgeScheduler::TotalSpawnWeight(
	ECataclysmDungeonSubType Excluded)
{
	// THE ENUM IN ITS DECLARED ORDER, STARTING PAST `None`. Every value from
	// `Timed` up to and including `Sacrificial`, which is the last one. Walking
	// the enum rather than a hand-written list means a sub-type added to it
	// counts as soon as `SpawnWeightFor` gives it a weight.
	//
	// **IT STARTS AT `Timed` RATHER THAN AT ZERO BECAUSE `None` IS NOT AN
	// OUTCOME.** Every dungeon a surge makes has a sub-type; see the weights in
	// the header. Beginning at zero and relying on a weight of zero to skip it
	// would work and would read as though the value were still in the running.
	float Total = 0.0f;

	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType Candidate =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (Candidate != Excluded)
		{
			Total += SpawnWeightFor(Candidate);
		}
	}

	return Total;
}

float UCataclysmSurgeScheduler::SpawnWeightBelow(
	ECataclysmDungeonSubType SubType)
{
	float Below = 0.0f;

	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value < static_cast<uint8>(SubType);
		 ++Value)
	{
		Below += SpawnWeightFor(static_cast<ECataclysmDungeonSubType>(Value));
	}

	return Below;
}

ECataclysmDungeonSubType UCataclysmSurgeScheduler::SubTypeAtPoint(
	float Point, ECataclysmDungeonSubType Excluded)
{
	const uint8 Last = static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
	ECataclysmDungeonSubType Furthest = ECataclysmDungeonSubType::None;

	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= Last; ++Value)
	{
		const ECataclysmDungeonSubType Candidate =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (Candidate == Excluded)
		{
			continue;
		}

		Furthest = Candidate;
		Point -= SpawnWeightFor(Candidate);

		if (Point < 0.0f)
		{
			return Candidate;
		}
	}

	// PAST THE END OF THE LINE. Reached when `FRand` returns exactly 1.0, which
	// its own documentation says it does not, and by any caller passing a point
	// larger than the total. The last sub-type on the line is the honest answer;
	// `None` would not be, because the caller asked which of THESE a number
	// picks.
	return Furthest;
}

ECataclysmDungeonSubType UCataclysmSurgeScheduler::RollSubType(
	FRandomStream& Stream, bool bSiegeAllowed)
{
	const float Total = TotalSpawnWeight();

	// EVERY WEIGHT ZERO WOULD DIVIDE BY NOTHING. It cannot happen with the
	// constants in the header and it is one line to be safe about. A dungeon
	// with no sub-type is still the honest answer when nothing can be chosen,
	// even though nothing can reach it: the alternative is naming one sub-type
	// as the answer to "which of these zero options", which would be a lie.
	if (Total <= 0.0f)
	{
		return ECataclysmDungeonSubType::None;
	}

	// ONE DRAW, AND THE ONLY ONE. See the header.
	const float Point = Stream.FRand() * Total;
	const ECataclysmDungeonSubType Chosen = SubTypeAtPoint(Point);

	if (bSiegeAllowed || Chosen != ECataclysmDungeonSubType::Siege)
	{
		return Chosen;
	}

	// **THE SAME DRAW, READ A SECOND TIME INTO A SMALLER LINE.** The city
	// already holds a Siege, and the owner's ruling is that this dungeon is
	// spread across the other six rather than made plain or rolled again. See
	// `SiegesPerCity`.
	//
	// WHY RE-READING IS EXACT AND NOT AN APPROXIMATION. `Point` is uniform over
	// the whole line, so given that it landed inside Siege's own band it is
	// uniform over that band. Dividing by the band's width turns it into a
	// number uniform over 0 to 1, and multiplying by the total of the other six
	// turns that into a point uniform over a line made of exactly those six.
	// Each therefore takes a share of the refusals in proportion to its weight,
	// which is what "spread it across the others" asks for -- and it costs no
	// further randomness, because no further randomness is drawn.
	const float BandStart =
		SpawnWeightBelow(ECataclysmDungeonSubType::Siege);
	const float BandWidth =
		SpawnWeightFor(ECataclysmDungeonSubType::Siege);

	if (BandWidth <= 0.0f)
	{
		// SIEGE IS NOT ON THE LINE AT ALL, so `Chosen` could not have been one
		// and this is unreachable. It is here because dividing by it is not.
		return Chosen;
	}

	// CLAMPED BELOW ONE so the re-read point cannot land past the end of the
	// smaller line. `Point` is inside the band by construction; the clamp is
	// against floating-point arithmetic at its very edge, not against a real
	// case.
	const float Across = FMath::Clamp((Point - BandStart) / BandWidth, 0.0f,
									  1.0f - KINDA_SMALL_NUMBER);

	return SubTypeAtPoint(
		Across * TotalSpawnWeight(ECataclysmDungeonSubType::Siege),
		ECataclysmDungeonSubType::Siege);
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
	FCataclysmDungeonSpec Spec;

	switch (Type)
	{
	case ECataclysmDungeonType::Basic:
		// THE DAMAGE COLUMNS ARE POINTS AND PEOPLE, NOT FRACTIONS OF THE CITY.
		// Issue #1331, and `config.DUNGEON_SPECS` holds the same four pairs.
		// Each one is the fraction it replaced multiplied by that tier's base
		// maximum, so this table reproduces the old arithmetic exactly for a
		// city at its base size and the change of shape can be measured on its
		// own before any number is chosen:
		//
		//   Outpost    10% of  1,000 =   100 defence,  5% of   5,000 =   250
		//   Bulwark      9% of 3,000 =   270 defence,  5% of  20,000 = 1,000
		//   Sanctuary    8% of 8,000 =   640 defence,  4% of  60,000 = 2,400
		//   Pillar     6% of 20,000 = 1,200 defence,  3% of 150,000 = 4,500
		//
		// THE TIER MAXIMUMS ARE `UCataclysmEmpireMap::OutpostMaxDefence` and
		// its seven siblings. `tools/tests/test_surge_port.py` ties these four
		// pairs to the model's and to a third hand-written copy.
		switch (Tier)
		{
		case ECataclysmCityTier::Bulwark:
			Spec.LeastFloors = 15;
			Spec.MostFloors = 25;
			Spec.DefenceDamage = 270.0f;
			Spec.PopulationDamage = 1000.0f;
			break;

		case ECataclysmCityTier::Sanctuary:
			Spec.LeastFloors = 25;
			Spec.MostFloors = 40;
			Spec.DefenceDamage = 640.0f;
			Spec.PopulationDamage = 2400.0f;
			break;

		case ECataclysmCityTier::Pillar:
			Spec.LeastFloors = 40;
			Spec.MostFloors = 60;
			Spec.DefenceDamage = 1200.0f;
			Spec.PopulationDamage = 4500.0f;
			break;

		default:
			Spec.LeastFloors = 8;
			Spec.MostFloors = 15;
			Spec.DefenceDamage = 100.0f;
			Spec.PopulationDamage = 250.0f;
			break;
		}
		break;

	case ECataclysmDungeonType::Quest:
		// THE ZERO DAMAGE IS THE DESIGN AND NOT A MISSING NUMBER. A Quest
		// dungeon "does not resolve -- refreshes and may move to adjacent
		// city", so it never applies a consequence to the city it sits on.
		// `Simulation._resolve` returns before touching defence or population
		// for one, and every Quest row in `config.DUNGEON_SPECS` is 0.0/0.0.
		switch (Tier)
		{
		case ECataclysmCityTier::Bulwark:
			Spec.LeastFloors = 30;
			Spec.MostFloors = 45;
			break;

		case ECataclysmCityTier::Sanctuary:
			Spec.LeastFloors = 30;
			Spec.MostFloors = 50;
			break;

		case ECataclysmCityTier::Pillar:
			Spec.LeastFloors = 50;
			Spec.MostFloors = 70;
			break;

		default:
			Spec.LeastFloors = 20;
			Spec.MostFloors = 30;
			break;
		}
		break;

	case ECataclysmDungeonType::FallenCity:
		// DEEPER THAN A BASIC DUNGEON ON THE SAME CITY, ROUGHLY TWO AND A HALF
		// TIMES. The design's minimums are "20/40/60 for
		// Outpost/Bulwark/Sanctuary" and these ranges start exactly there.
		//
		// AND IT BITES NOTHING, because the city it stands on has already
		// fallen. `Dungeon.resolves` in the model says so: "Fallen City and
		// Cataclysm dungeons have already done their damage."
		switch (Tier)
		{
		case ECataclysmCityTier::Bulwark:
			Spec.LeastFloors = 40;
			Spec.MostFloors = 60;
			break;

		case ECataclysmCityTier::Sanctuary:
			Spec.LeastFloors = 60;
			Spec.MostFloors = 85;
			break;

		case ECataclysmCityTier::Pillar:
			Spec.LeastFloors = 80;
			Spec.MostFloors = 120;
			break;

		default:
			Spec.LeastFloors = 20;
			Spec.MostFloors = 35;
			break;
		}
		break;

	case ECataclysmDungeonType::Cataclysm:
		// AT THE PILLAR AND NOWHERE ELSE, AND EVERY OTHER TIER IS LEFT UNBUILT
		// DELIBERATELY. `config.DUNGEON_SPECS` holds exactly one Cataclysm row,
		// `(CATACLYSM, PILLAR)`, and `TuningConfig.spec` is a bare dictionary
		// lookup -- so asking the model for a Cataclysm on an Outpost raises
		// rather than answering. A spec whose `IsBuilt` is false is the same
		// answer here, where a lookup cannot raise.
		if (Tier == ECataclysmCityTier::Pillar)
		{
			Spec.LeastFloors = 100;
			Spec.MostFloors = 150;
		}
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

FCataclysmDungeon UCataclysmSurgeScheduler::MakeFallenCityDungeon(
	int32 DungeonId, const FCataclysmCity& City, int32 Day,
	int32 DungeonsAbsorbed)
{
	const FCataclysmDungeonSpec Spec =
		SpecFor(ECataclysmDungeonType::FallenCity, City.Tier);

	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = DungeonId;
	Dungeon.Type = ECataclysmDungeonType::FallenCity;
	Dungeon.CityId = City.CityId;
	Dungeon.CityTier = City.Tier;
	Dungeon.SpawnedDay = Day;

	// AT LEAST ONE, THOUGH IT SHOULD NEVER HAVE TO BE. A city falls because a
	// dungeon standing on it resolved, and that dungeon is absorbed along with
	// the rest, so the count is one or more by the time this is called. A
	// bossless, zero-floor dungeon would be worse than a floor of one.
	const int32 Absorbed = FMath::Max(1, DungeonsAbsorbed);

	// THE FLOOR IS THE SPEC'S SHALLOW END, which is the design's 20/40/60. A
	// city that fell carrying more dungeons than that is deeper than the
	// minimum; a quiet one is exactly it.
	Dungeon.Floors = FMath::Max(Absorbed, Spec.LeastFloors);

	// ONE BOSS PER DUNGEON THAT WAS STANDING, which is the same count and not
	// the same number as the floors: a city that fell holding three dungeons is
	// a twenty floor dungeon with three bosses in it.
	Dungeon.Bosses = Absorbed;

	// IT NEVER RESOLVES, so its timer is set past the end of any run rather
	// than derived from its depth.
	Dungeon.ResolveDays = FallenCityResolveDays;

	// AND IT TAKES NOTHING. `SpecFor` answers zero for both, and copying them
	// rather than writing zeroes here means a change to the spec cannot leave
	// this behind.
	Dungeon.DefenceDamage = Spec.DefenceDamage;
	Dungeon.PopulationDamage = Spec.PopulationDamage;

	// NO SUB-TYPE. A Fallen City is what a city became rather than something a
	// surge rolled, and the sub-types are what a surge rolls.
	Dungeon.SubType = ECataclysmDungeonSubType::None;

	return Dungeon;
}

ECataclysmDungeonType UCataclysmSurgeScheduler::RollKind(FRandomStream& Stream)
{
	// STRICTLY LESS THAN, which is what `self.rng.random() < quest_dungeon_chance`
	// is in `Simulation.trigger_surge`. `FRandomStream::FRand` draws from [0, 1)
	// exactly as `random.random` does, so a chance of zero can never come out
	// true and a chance of one always does.
	return Stream.FRand() < QuestChance
		? ECataclysmDungeonType::Quest
		: ECataclysmDungeonType::Basic;
}

FCataclysmDungeon UCataclysmSurgeScheduler::MakeDungeon(
	int32 DungeonId, const FCataclysmCity& City, int32 Day,
	FRandomStream& Stream, bool bSiegeAllowed, ECataclysmDungeonType Type) const
{
	// A SURGE ROLLS TWO KINDS AND `RollKind` IS WHERE THAT IS DECIDED. A Fallen
	// City is built by `MakeFallenCityDungeon` from what the city was carrying,
	// and nothing builds a Cataclysm yet -- so neither reaches here. Asked for
	// one anyway, this builds a Basic rather than a dungeon carrying a Fallen
	// City's floors and a Quest's timer, which is a shape nothing in the design
	// describes.
	const ECataclysmDungeonType Kind =
		(Type == ECataclysmDungeonType::Quest)
			? ECataclysmDungeonType::Quest
			: ECataclysmDungeonType::Basic;

	const FCataclysmDungeonSpec Spec = SpecFor(Kind, City.Tier);

	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = DungeonId;
	Dungeon.Type = Kind;

	// ONE BOSS, ON THE FINAL FLOOR. The design's universal rule, and set here
	// rather than left to the field's default so that a wave-rolled dungeon and
	// a Fallen City are visibly answering the same question differently. A Quest
	// dungeon is not the exception a Fallen City is: the design names only a
	// Dungeon City as having "multiple boss fights".
	Dungeon.Bosses = 1;
	Dungeon.CityId = City.CityId;
	Dungeon.CityTier = City.Tier;
	Dungeon.SpawnedDay = Day;
	Dungeon.DefenceDamage = Spec.DefenceDamage;
	Dungeon.PopulationDamage = Spec.PopulationDamage;

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

	// AND WHAT IT DOES DIFFERENTLY. Rolled after the depth so the depth is drawn
	// from the same place in the stream it always was, and before the walk cost
	// because Cow Level decides what that cost is.
	Dungeon.SubType = RollSubType(Stream, bSiegeAllowed);

	// AND HOW LONG WALKING IT COSTS, WHICH IS NOT THE SAME QUESTION AS HOW DEEP
	// IT IS. One floor costs one day to begin with, so this starts at the floor
	// count; "Dungeons here take 4 less days to beat" lowers it WITHOUT lowering
	// the floor count, which is what lets an invested player make a fifty floor
	// dungeon cost two days rather than fifty. The dungeon is still fifty floors
	// deep, still worth what that is worth, and still bites on the schedule
	// below.
	//
	// A MINIMUM OF ONE DAY, which the sheet states. A dungeon that cost no time
	// at all would be free, and the empire layer's whole tension is that nothing
	// is.
	const float Quicker = City.UpgradeValueFor(
		ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer);

	// EXCEPT ON A COW LEVEL, WHERE THE UPGRADE IS NOT READ AT ALL. "Time to
	// complete is doubled and cannot be reduced" is two rules, and doubling the
	// already-reduced figure would honour only the first: a city that bought the
	// walk-days upgrade would still get its four days off, halved. So the
	// doubling is applied to what the depth alone costs.
	//
	// IT IS STILL THE SLOWEST DUNGEON THERE IS, which is the trade the design
	// document sets against "ridiculous amounts of loot".
	Dungeon.WalkDays =
		(Dungeon.SubType == ECataclysmDungeonSubType::CowLevel)
			? FMath::Max(1.0f, Dungeon.Floors * UCataclysmDayClock::DaysPerFloor)
				  * CowLevelWalkMultiplier
			: FMath::Max(
				  1.0f,
				  Dungeon.Floors * UCataclysmDayClock::DaysPerFloor - Quicker);

	// THE TIMER COMES FROM THE DEPTH AND THEN VARIES. `ResolveDaysFor` is the
	// day clock's; the jitter is what stops two dungeons of one depth coming due
	// on the same day. See `ResolveJitter`.
	//
	// SO THE FLOOR UPGRADES MOVE THE TIMER TOO, and that is the point rather
	// than a side effect. Depth and reward are the same axis, which `CLAUDE.md`
	// states: a deeper dungeon is worth more and slower to bite, and a shallower
	// one is poorer and bites sooner. Adjusting the timer separately would break
	// the trade the whole strategy layer rests on.
	//
	// THE WALK COST ABOVE IS NOT AN INPUT HERE, AND MUST NOT BECOME ONE. Depth
	// and time come apart as soon as a player invests, so a dungeon made quicker
	// to walk keeps the timer its depth earned. A Cow Level takes twice as long
	// to walk and bites on exactly the same day it would have.
	const float Base = UCataclysmDayClock::ResolveDaysFor(Dungeon.Floors);
	const float Jitter = 1.0f + Stream.FRandRange(-ResolveJitter, ResolveJitter);

	// EXCEPT FOR A QUEST DUNGEON, WHOSE TIMER IS NOT A BITE SCHEDULE. It takes
	// nothing from its host whenever it runs out, so there is no bite for its
	// depth to be traded against; what the timer decides is how long the player
	// has before the objective moves. `QuestResolveDays` is the flat figure the
	// model gives on every Quest row, and the paragraph there is the argument
	// for why this is not the depth rule above being broken.
	//
	// THE DRAWS HAPPEN EITHER WAY, AND THAT IS DELIBERATE. `Base` and `Jitter`
	// are computed above rather than inside the branch so that a Quest dungeon
	// takes exactly as many numbers off the stream as a Basic one. Skipping the
	// jitter draw for a Quest would make every later roll in the run depend on
	// which kinds came out earlier, and nothing about a Quest dungeon should
	// change what the dungeon after it looks like.
	Dungeon.ResolveDays = (Kind == ECataclysmDungeonType::Quest)
		? QuestResolveDays
		: Base * Jitter;

	return Dungeon;
}

TArray<FCataclysmDungeon> UCataclysmSurgeScheduler::RollWave(
	const UCataclysmEmpireMap& Map, int32 Day, int32 FirstDungeonId,
	FRandomStream& Stream, const TArray<int32>& DungeonsPerCity,
	const TArray<int32>& SiegesPerCityNow) const
{
	TArray<FCataclysmDungeon> Wave;

	const TArray<int32> Targets =
		PickTargets(Map, DungeonsInNextSurge(), Stream, DungeonsPerCity);

	Wave.Reserve(Targets.Num());

	// COPIED SO THIS WAVE'S OWN SIEGES COUNT TOWARDS THE CAP. Two dungeons in
	// one wave can land on the same city -- `PickTargets` rolls with replacement
	// -- so counting only what was already standing would let a single wave put
	// two Sieges on one city, which is the thing the cap forbids.
	TArray<int32> Sieges = SiegesPerCityNow;

	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		const FCataclysmCity* City = Map.Find(Targets[Index]);
		if (City == nullptr)
		{
			continue;
		}

		// **ASKED BEFORE THE DUNGEON IS MADE, NOT CORRECTED AFTERWARDS.** The
		// roll needs to know whether a Siege is allowed here, because when it is
		// not it re-reads its own draw into the other six sub-types rather than
		// taking a second one. See `UCataclysmSurgeScheduler::RollSubType`.
		//
		// AN EMPTY LIST MEANS THE CALLER DID NOT SAY, and then nothing is
		// refused on account of what was already standing. Sieges this wave
		// makes are still counted, because `Sieges` is grown to fit below.
		const bool bSiegeAllowed =
			!Sieges.IsValidIndex(City->CityId)
			|| Sieges[City->CityId] < SiegesPerCity;

		// WHICH KIND, BEFORE ANYTHING ABOUT THE DUNGEON IS DRAWN. A Quest
		// dungeon's floors come from a different row of `SpecFor`, so the kind
		// has to be settled before the depth is rolled rather than corrected
		// after. `Simulation.trigger_surge` decides it in the same place and for
		// the same reason, one line before it calls `_make_dungeon`.
		//
		// THE CITY DOES NOT AFFECT IT. Every exposed city is as likely to
		// receive a Quest dungeon as any other; the design gives no rule saying
		// otherwise and the model reads only the chance. What the city DOES
		// affect is how deep it is, through the tier's Quest row.
		const ECataclysmDungeonType Kind = RollKind(Stream);

		FCataclysmDungeon Dungeon = MakeDungeon(FirstDungeonId + Index, *City,
												Day, Stream, bSiegeAllowed,
												Kind);

		if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
		{
			if (!Sieges.IsValidIndex(Dungeon.CityId))
			{
				Sieges.SetNumZeroed(
					FMath::Max(Sieges.Num(), Dungeon.CityId + 1));
			}

			++Sieges[Dungeon.CityId];
		}

		Wave.Add(Dungeon);
	}

	return Wave;
}
