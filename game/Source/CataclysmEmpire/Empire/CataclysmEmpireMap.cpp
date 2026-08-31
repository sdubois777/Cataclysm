// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmEmpireMap.h"

// ---------------------------------------------------------------------------
// The shape
// ---------------------------------------------------------------------------

int32 UCataclysmEmpireMap::CityCountForRing(int32 Ring)
{
	if (Ring < 0 || Ring > Radius)
	{
		return 0;
	}

	// FOUR PER RING, WHICH IS A PROPERTY OF THE TAXICAB BALL AND NOT A TABLE.
	// The cells at distance N from the centre are the four diagonal edges of a
	// diamond N across, and they hold 4N cells between them. Ring 0 is the one
	// exception, because a diamond of no size is a single point.
	return Ring == 0 ? 1 : 4 * Ring;
}

ECataclysmCityTier UCataclysmEmpireMap::TierForRing(int32 Ring)
{
	const int32 Clamped = FMath::Clamp(Ring, 0, Radius);
	return static_cast<ECataclysmCityTier>(Radius - Clamped);
}

int32 UCataclysmEmpireMap::RingForTier(ECataclysmCityTier Tier)
{
	return Radius - static_cast<int32>(Tier);
}

FString UCataclysmEmpireMap::TierName(ECataclysmCityTier Tier)
{
	switch (Tier)
	{
	case ECataclysmCityTier::Bulwark:	return TEXT("Bulwark");
	case ECataclysmCityTier::Sanctuary:	return TEXT("Sanctuary");
	case ECataclysmCityTier::Pillar:	return TEXT("Pillar");
	default:							return TEXT("Outpost");
	}
}

float UCataclysmEmpireMap::MaxDefenceFor(ECataclysmCityTier Tier)
{
	switch (Tier)
	{
	case ECataclysmCityTier::Bulwark:	return BulwarkMaxDefence;
	case ECataclysmCityTier::Sanctuary:	return SanctuaryMaxDefence;
	case ECataclysmCityTier::Pillar:	return PillarMaxDefence;
	default:							return OutpostMaxDefence;
	}
}

float UCataclysmEmpireMap::MaxPopulationFor(ECataclysmCityTier Tier)
{
	switch (Tier)
	{
	case ECataclysmCityTier::Bulwark:	return BulwarkMaxPopulation;
	case ECataclysmCityTier::Sanctuary:	return SanctuaryMaxPopulation;
	case ECataclysmCityTier::Pillar:	return PillarMaxPopulation;
	default:							return OutpostMaxPopulation;
	}
}

// ---------------------------------------------------------------------------
// Building it
// ---------------------------------------------------------------------------

void UCataclysmEmpireMap::Build()
{
	Cities.Reset();
	PillarId = INDEX_NONE;

	// ROW BY ROW AND THEN COLUMN BY COLUMN, which is the order
	// `world.build_empire` walks the lattice in. Identifiers follow that walk,
	// so city 12 is the Pillar here for the same reason it is there.
	for (int32 R = -Radius; R <= Radius; ++R)
	{
		for (int32 C = -Radius; C <= Radius; ++C)
		{
			const int32 Ring = FMath::Abs(R) + FMath::Abs(C);
			if (Ring > Radius)
			{
				continue;
			}

			FCataclysmCity City;
			City.CityId = Cities.Num();
			City.Tier = TierForRing(Ring);
			City.R = R;
			City.C = C;
			City.Name = FString::Printf(TEXT("%s (%d,%d)"), *TierName(City.Tier), R, C);
			City.MaxDefence = MaxDefenceFor(City.Tier);
			City.MaxPopulation = MaxPopulationFor(City.Tier);
			City.Defence = City.MaxDefence;
			City.Population = City.MaxPopulation;

			if (Ring == 0)
			{
				PillarId = City.CityId;
			}

			Cities.Add(MoveTemp(City));
		}
	}

	// LANES. Every orthogonal step changes the ring by exactly one, so a
	// neighbour is either one step further out -- and therefore shielding this
	// city -- or one step further in.
	static const int32 Steps[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

	for (int32 Index = 0; Index < Cities.Num(); ++Index)
	{
		FCataclysmCity& City = Cities[Index];
		const int32 Ring = City.Ring();

		for (const int32(&Step)[2] : Steps)
		{
			const int32 Neighbour = CityAt(City.R + Step[0], City.C + Step[1]);
			if (Neighbour == INDEX_NONE)
			{
				continue;
			}

			if (Cities[Neighbour].Ring() > Ring)
			{
				City.Outward.Add(Neighbour);
			}
			else
			{
				City.Inward.Add(Neighbour);
			}
		}
	}

	// THE CURVED EDGES OF THE DIAMOND. Rim Outposts are also linked to the rim
	// Outposts beside them, which are a diagonal step away in lattice space.
	// Those links are same-ring, so they carry no lane inward; see
	// `FCataclysmCity::Perimeter`.
	static const int32 Diagonals[4][2] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };

	for (int32 Index = 0; Index < Cities.Num(); ++Index)
	{
		FCataclysmCity& City = Cities[Index];
		if (City.Ring() != Radius)
		{
			continue;
		}

		for (const int32(&Step)[2] : Diagonals)
		{
			const int32 Neighbour = CityAt(City.R + Step[0], City.C + Step[1]);
			if (Neighbour != INDEX_NONE && Cities[Neighbour].Ring() == Radius)
			{
				City.Perimeter.Add(Neighbour);
			}
		}
	}
}

const FCataclysmCity* UCataclysmEmpireMap::Find(int32 CityId) const
{
	return Cities.IsValidIndex(CityId) ? &Cities[CityId] : nullptr;
}

FCataclysmCity* UCataclysmEmpireMap::FindMutable(int32 CityId)
{
	return Cities.IsValidIndex(CityId) ? &Cities[CityId] : nullptr;
}

int32 UCataclysmEmpireMap::CityAt(int32 InR, int32 InC) const
{
	if (FMath::Abs(InR) + FMath::Abs(InC) > Radius)
	{
		return INDEX_NONE;
	}

	// A LINEAR SEARCH OVER TWENTY-FIVE CITIES, and deliberately not a map. The
	// whole empire is smaller than one cache line's worth of work, and a
	// coordinate lookup happens when a map is built or a lane is walked, never
	// per frame. A second container would be a second thing to keep in step.
	for (const FCataclysmCity& City : Cities)
	{
		if (City.R == InR && City.C == InC)
		{
			return City.CityId;
		}
	}

	return INDEX_NONE;
}

// ---------------------------------------------------------------------------
// Lanes
// ---------------------------------------------------------------------------

bool UCataclysmEmpireMap::IsExposed(int32 CityId) const
{
	const FCataclysmCity* City = Find(CityId);
	if (City == nullptr || City->bFallen)
	{
		// NOTHING LEFT THERE TO ATTACK. A fallen city is a Dungeon City, and
		// what stands on it is one dungeon of its own rather than a target for
		// the next surge.
		return false;
	}

	if (City->Ring() == Radius)
	{
		return true;
	}

	for (const int32 Shield : City->Outward)
	{
		if (Cities.IsValidIndex(Shield) && Cities[Shield].bFallen)
		{
			return true;
		}
	}

	return false;
}

TArray<int32> UCataclysmEmpireMap::ExposedCities(bool bIncludePillar) const
{
	TArray<int32> Exposed;

	for (const FCataclysmCity& City : Cities)
	{
		if (!bIncludePillar && City.CityId == PillarId)
		{
			continue;
		}

		if (IsExposed(City.CityId))
		{
			Exposed.Add(City.CityId);
		}
	}

	return Exposed;
}

bool UCataclysmEmpireMap::IsPillarExposed() const
{
	return IsExposed(PillarId);
}

TArray<int32> UCataclysmEmpireMap::FallCost(int32 ExtraFallen) const
{
	TArray<int32> Cost;
	Cost.Init(0, Cities.Num());

	// OUTERMOST RING FIRST, so that by the time an inner city is reached every
	// city shielding it already has an answer. Any other order would read an
	// uncomputed cost.
	for (int32 Ring = Radius; Ring >= 0; --Ring)
	{
		for (const FCataclysmCity& City : Cities)
		{
			if (City.Ring() != Ring)
			{
				continue;
			}

			if (City.bFallen || City.CityId == ExtraFallen)
			{
				Cost[City.CityId] = 0;
			}
			else if (Ring == Radius)
			{
				// Already exposed. It only has to break.
				Cost[City.CityId] = 1;
			}
			else
			{
				int32 Cheapest = MAX_int32;
				for (const int32 Shield : City.Outward)
				{
					if (Cost.IsValidIndex(Shield))
					{
						Cheapest = FMath::Min(Cheapest, Cost[Shield]);
					}
				}

				Cost[City.CityId] = Cheapest == MAX_int32 ? 1 : 1 + Cheapest;
			}
		}
	}

	return Cost;
}

int32 UCataclysmEmpireMap::DistanceToDefeat() const
{
	return DistanceToDefeatIf(INDEX_NONE);
}

int32 UCataclysmEmpireMap::DistanceToDefeatIf(int32 ExtraFallen) const
{
	const TArray<int32> Cost = FallCost(ExtraFallen);

	// MEASURED AT RING 1 AND NOT AT THE PILLAR. The run is lost the moment a
	// Sanctuary falls, because that is when the path is clear; the Pillar itself
	// does not have to break for it.
	int32 Nearest = MAX_int32;
	for (const FCataclysmCity& City : Cities)
	{
		if (City.Ring() == 1)
		{
			Nearest = FMath::Min(Nearest, Cost[City.CityId]);
		}
	}

	// A MAP WITH NO SANCTUARIES ANSWERS AS THOUGH IT WERE INTACT, and the only
	// map like that is one `Build` has not been called on. Answering 0 would be
	// the arithmetic result of a minimum over nothing and would tell a caller
	// checking for the loss condition that the run is already over.
	return Nearest == MAX_int32 ? Radius : Nearest;
}

TArray<int32> UCataclysmEmpireMap::LaneCriticality() const
{
	TArray<int32> Criticality;
	Criticality.Init(0, Cities.Num());

	const int32 Base = DistanceToDefeat();

	for (const FCataclysmCity& City : Cities)
	{
		if (City.bFallen || City.CityId == PillarId)
		{
			// ALREADY GONE, OR THE THING BEING DEFENDED. Neither can move the
			// number by falling.
			continue;
		}

		Criticality[City.CityId] =
			FMath::Max(0, Base - DistanceToDefeatIf(City.CityId));
	}

	return Criticality;
}

int32 UCataclysmEmpireMap::OpenLanes() const
{
	int32 Open = 0;

	for (const FCataclysmCity& City : Cities)
	{
		if (City.Ring() < Radius && IsExposed(City.CityId))
		{
			++Open;
		}
	}

	return Open;
}

int32 UCataclysmEmpireMap::BreachDepth() const
{
	int32 Depth = 0;

	for (const FCataclysmCity& City : Cities)
	{
		if (City.bFallen)
		{
			Depth = FMath::Max(Depth, Radius + 1 - City.Ring());
		}
	}

	return Depth;
}

// ---------------------------------------------------------------------------
// The state of the empire
// ---------------------------------------------------------------------------

float UCataclysmEmpireMap::TotalPopulation() const
{
	float Total = 0.0f;

	for (const FCataclysmCity& City : Cities)
	{
		if (City.IsAlive())
		{
			Total += City.Population;
		}
	}

	return Total;
}

float UCataclysmEmpireMap::TotalMaxPopulation() const
{
	float Total = 0.0f;

	// EVERY CITY, FALLEN OR NOT. This is what the empire would hold intact, so
	// it is the denominator the fraction lost is measured against and it must
	// not shrink as cities are lost.
	for (const FCataclysmCity& City : Cities)
	{
		Total += City.MaxPopulation;
	}

	return Total;
}

int32 UCataclysmEmpireMap::FallenCityCount() const
{
	int32 Fallen = 0;

	for (const FCataclysmCity& City : Cities)
	{
		if (City.bFallen)
		{
			++Fallen;
		}
	}

	return Fallen;
}

TArray<int32> UCataclysmEmpireMap::AliveCities(bool bIncludePillar) const
{
	TArray<int32> Alive;

	for (const FCataclysmCity& City : Cities)
	{
		if (!bIncludePillar && City.CityId == PillarId)
		{
			continue;
		}

		if (City.IsAlive())
		{
			Alive.Add(City.CityId);
		}
	}

	return Alive;
}

// ---------------------------------------------------------------------------
// What happens to a city
// ---------------------------------------------------------------------------

bool UCataclysmEmpireMap::Bite(int32 CityId, float DefenceFraction,
							   float PopulationFraction)
{
	FCataclysmCity* City = FindMutable(CityId);
	if (City == nullptr || City->bFallen)
	{
		return false;
	}

	City->Defence -= City->MaxDefence * FMath::Max(0.0f, DefenceFraction);
	City->Population -= City->MaxPopulation * FMath::Max(0.0f, PopulationFraction);
	City->Population = FMath::Max(0.0f, City->Population);

	if (City->Defence <= 0.0f)
	{
		return Fall(CityId);
	}

	return false;
}

bool UCataclysmEmpireMap::Fall(int32 CityId)
{
	FCataclysmCity* City = FindMutable(CityId);
	if (City == nullptr || City->bFallen)
	{
		return false;
	}

	City->Defence = 0.0f;
	City->bFallen = true;

	// THE VOID'S CITIES DO NOT COME BACK. Nothing marks a city doomed yet; see
	// `FCataclysmCity::bDoomed`.
	if (City->bDoomed)
	{
		City->bErased = true;
	}

	return true;
}

bool UCataclysmEmpireMap::Retake(int32 CityId)
{
	FCataclysmCity* City = FindMutable(CityId);
	if (City == nullptr || !City->bFallen || City->bErased)
	{
		return false;
	}

	City->bFallen = false;
	City->Defence = City->MaxDefence * RetakenFraction;
	City->Population = City->MaxPopulation * RetakenFraction;

	return true;
}

// ---------------------------------------------------------------------------
// Seeing it
// ---------------------------------------------------------------------------

FString UCataclysmEmpireMap::Render() const
{
	TArray<FString> Rows;

	for (int32 R = -Radius; R <= Radius; ++R)
	{
		// THE INDENT IS ONE SPACE PER ROW OFF CENTRE, and it is joined into the
		// row as though it were a cell, which is what makes the leading gap on
		// the first row four characters rather than three. That is what
		// `Empire.render` produces and this has to match it character for
		// character to be worth anything as a comparison.
		TArray<FString> Row;
		Row.Add(FString::ChrN(FMath::Abs(R), TEXT(' ')));

		for (int32 C = -Radius; C <= Radius; ++C)
		{
			const int32 CityId = CityAt(R, C);
			if (CityId == INDEX_NONE)
			{
				continue;
			}

			const FCataclysmCity& City = Cities[CityId];
			const TCHAR Mark = City.bFallen ? TEXT('x')
				: IsExposed(CityId) ? TEXT('!')
				: TEXT('.');

			Row.Add(FString::Printf(TEXT("%c%c"),
									TierName(City.Tier)[0], Mark));
		}

		Rows.Add(FString::Join(Row, TEXT(" ")));
	}

	return FString::Join(Rows, TEXT("\n"));
}
