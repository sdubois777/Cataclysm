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

			// FULL AT THE MAXIMUM WITH THE EMPIRE UPGRADE TREE, so a run begun with the tree starts every city full at
			// the raised figure, as the simulation's `build_empire` does (S1). The tree is the run's, handed to this map
			// before `Build`; a map built on its own has none. Issue #2741 slice 3a.
			City.Defence = EffectiveMaxDefence(City);
			City.Population = EffectiveMaxPopulation(City);

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
			Total += PopulationOf(City);
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
		Total += EffectiveMaxPopulation(City);
	}

	return Total;
}

// ---------------------------------------------------------------------------
// What the empire upgrade tree makes of a city -- issue #2741 slice 3a
// ---------------------------------------------------------------------------

float UCataclysmEmpireMap::EffectiveMaxDefence(const FCataclysmCity& City) const
{
	return City.MaxDefence * TreeBonuses.MaxDefenceFactorFor(City.Tier, City.Ring(), CitiesWithADefensiveUpgrade());
}

float UCataclysmEmpireMap::EffectiveMaxPopulation(const FCataclysmCity& City) const
{
	return City.MaxPopulation * TreeBonuses.MaxPopulationFactorFor(City.Tier);
}

float UCataclysmEmpireMap::DefenceOf(const FCataclysmCity& City) const
{
	return FMath::Min(City.Defence, EffectiveMaxDefence(City));
}

float UCataclysmEmpireMap::PopulationOf(const FCataclysmCity& City) const
{
	return FMath::Min(City.Population, EffectiveMaxPopulation(City));
}

float UCataclysmEmpireMap::DefenceFractionOf(const FCataclysmCity& City) const
{
	const float Max = EffectiveMaxDefence(City);
	return Max <= 0.0f ? 0.0f : DefenceOf(City) / Max;
}

bool UCataclysmEmpireMap::IsADefensiveUpgrade(const FCataclysmCityUpgrade& Upgrade)
{
	return Upgrade.Effect == ECataclysmCityUpgradeEffect::MaxDefence
		|| Upgrade.Effect == ECataclysmCityUpgradeEffect::ResistDefenceLoss;
}

int32 UCataclysmEmpireMap::CitiesWithADefensiveUpgrade() const
{
	// NOTHING TO COUNT WITHOUT THE NODE, and the count is read for every city's maximum.
	if (TreeBonuses.MaxDefencePerDefendedCity == 0.0f)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FCataclysmCity& City : Cities)
	{
		if (City.IsAlive() && City.Upgrades.ContainsByPredicate(&UCataclysmEmpireMap::IsADefensiveUpgrade))
		{
			++Count;
		}
	}
	return Count;
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

bool UCataclysmEmpireMap::Damage(int32 CityId, float DefencePoints,
								 float PopulationPoints)
{
	FCataclysmCity* City = FindMutable(CityId);
	if (City == nullptr || City->bFallen)
	{
		return false;
	}

	// THE CITY'S OWN RESISTANCES, IF IT BOUGHT THEM. "This city resists 25% of
	// damage" takes a quarter off what a dungeon takes. Clamped at 1 so a stack
	// of them can at worst make the city untouchable rather than heal it; no
	// combination in the sheet reaches 1, and a resistance that healed would be
	// a silent reversal of what the upgrade says.
	const float DefenceResisted = FMath::Clamp(
		City->UpgradeValueFor(ECataclysmCityUpgradeEffect::ResistDefenceLoss),
		0.0f, 1.0f);

	const float PopulationResisted = FMath::Clamp(
		City->UpgradeValueFor(ECataclysmCityUpgradeEffect::ResistPopulationLoss),
		0.0f, 1.0f);

	// THE CITY'S MAXIMUM IS DELIBERATELY ABSENT FROM BOTH LINES. That is issue
	// #1331: while it was a factor here it divided out of how long the city
	// survived, and raising a ceiling bought nothing.
	const float DefenceTaken =
		FMath::Max(0.0f, DefencePoints) * (1.0f - DefenceResisted);

	const float PopulationTaken =
		FMath::Max(0.0f, PopulationPoints) * (1.0f - PopulationResisted);

	// FROM WHAT THE CITY IS READ AS HOLDING, so a hit after a respec lowered a maximum starts from that maximum rather
	// than from a figure above it (S1). Issue #2741 slice 3a.
	City->Defence = DefenceOf(*City);
	City->Population = PopulationOf(*City);

	City->Defence -= DefenceTaken;
	City->Population -= PopulationTaken;
	City->Population = FMath::Max(0.0f, City->Population);

	if (City->Defence <= 0.0f)
	{
		return Fall(CityId);
	}

	return false;
}

bool UCataclysmEmpireMap::Bite(int32 CityId, float DefenceFraction,
							   float PopulationFraction)
{
	const FCataclysmCity* City = Find(CityId);
	if (City == nullptr || City->bFallen)
	{
		return false;
	}

	// TURNED INTO POINTS AND HANDED STRAIGHT ON, so the resistances, the clamp
	// on population and the fall check exist once rather than twice. See the
	// header: this path survives for the Siege sub-type's deliberate exception
	// and for nothing else.
	return Damage(CityId, EffectiveMaxDefence(*City) * FMath::Max(0.0f, DefenceFraction),
				  EffectiveMaxPopulation(*City) * FMath::Max(0.0f, PopulationFraction));
}

bool UCataclysmEmpireMap::AddVoidStacks(int32 CityId, int32 Stacks)
{
	FCataclysmCity* City = FindMutable(CityId);
	if (City == nullptr || City->bFallen)
	{
		return false;
	}

	// NO DEFENCE AND NO PEOPLE ARE TAKEN. That is the whole of the owner's
	// rule: the Void's ordinary dungeons add stacks "instead of doing damage to
	// the city or population". So the city's two resistances, which are read in
	// `Damage`, do not reach this.
	City->VoidStacks += FMath::Max(0, Stacks);

	if (City->VoidStacks < VoidStacksToErase)
	{
		return false;
	}

	// THE TENTH STACK. The mark first, so `Fall` erases rather than merely
	// fells, as it does for any doomed city.
	City->bDoomed = true;
	return Fall(CityId);
}

bool UCataclysmEmpireMap::LeaveEdict(int32 CityId, FName Edict, int32 Today)
{
	FCataclysmCity* City = FindMutable(CityId);
	if (City == nullptr || Edict.IsNone())
	{
		return false;
	}

	// ONE AT A TIME: the new one replaces the old and its days start again.
	City->LeftEdict = Edict;
	City->LeftEdictUntilDay = Today + LeftEdictDays;
	return true;
}

// ---------------------------------------------------------------------------
// City upgrades
// ---------------------------------------------------------------------------

bool FCataclysmCity::HasUpgrade(FName RowName) const
{
	return Upgrades.ContainsByPredicate(
		[RowName](const FCataclysmCityUpgrade& Held)
		{
			return Held.RowName == RowName;
		});
}

float FCataclysmCity::UpgradeValueFor(
	ECataclysmCityUpgradeEffect Effect) const
{
	float Total = 0.0f;

	for (const FCataclysmCityUpgrade& Held : Upgrades)
	{
		if (Held.Effect == Effect)
		{
			Total += Held.Value;
		}
	}

	return Total;
}

int32 UCataclysmEmpireMap::FreeUpgradeSlots(int32 CityId) const
{
	const FCataclysmCity* City = Find(CityId);
	if (City == nullptr)
	{
		return 0;
	}

	return FMath::Max(0, UpgradeSlots - City->Upgrades.Num());
}

bool UCataclysmEmpireMap::AddUpgrade(int32 CityId,
									 const FCataclysmCityUpgrade& Upgrade)
{
	FCataclysmCity* City = FindMutable(CityId);
	if (City == nullptr)
	{
		return false;
	}

	// THE MAXIMA WITH THE EMPIRE UPGRADE TREE BEFORE THE UPGRADE, so a raise below adds what the effective maximum rose
	// by. Read before the upgrade is recorded: recording it can make this city one The Royal Engineer's Blessing counts.
	// Issue #2741 slice 3a.
	const float DefenceMaxBefore = EffectiveMaxDefence(*City);
	const float PopulationMaxBefore = EffectiveMaxPopulation(*City);

	City->Upgrades.Add(Upgrade);

	switch (Upgrade.Effect)
	{
	case ECataclysmCityUpgradeEffect::MaxDefence:
	{
		// CURRENT DEFENCE RISES BY THE SAME ABSOLUTE AMOUNT. See the header: a
		// full city has to stay full, and a damaged one has to stay short by
		// what it was short by. The amount is what the maximum WITH THE TREE
		// rose by, read from what the city is read as holding.
		City->MaxDefence += City->MaxDefence * Upgrade.Value;
		City->Defence = DefenceOf(*City) + FMath::Max(0.0f, EffectiveMaxDefence(*City) - DefenceMaxBefore);
		break;
	}

	case ECataclysmCityUpgradeEffect::MaxPopulation:
	{
		City->MaxPopulation += City->MaxPopulation * Upgrade.Value;
		City->Population = PopulationOf(*City) + FMath::Max(0.0f, EffectiveMaxPopulation(*City) - PopulationMaxBefore);
		break;
	}

	case ECataclysmCityUpgradeEffect::RestoreDefence:
	{
		// A ONE-TIME RESTORE, AND IT CANNOT OVERFILL. "Restore city's defenses
		// by 50%" on a city at 80% leaves it at 100 rather than 130. Of the
		// maximum with the empire upgrade tree.
		const float Max = EffectiveMaxDefence(*City);
		City->Defence = FMath::Min(Max, DefenceOf(*City) + Max * Upgrade.Value);
		break;
	}

	case ECataclysmCityUpgradeEffect::RestorePopulation:
	{
		const float Max = EffectiveMaxPopulation(*City);
		City->Population = FMath::Min(Max, PopulationOf(*City) + Max * Upgrade.Value);
		break;
	}

	default:
		// EVERY OTHER EFFECT IS SOMEBODY ELSE'S. The two resistances are read in
		// `Damage`, the two interval heals and the two dungeon effects are the
		// run's, and the rest are not built. Doing nothing here is correct
		// rather than a gap.
		break;
	}

	return true;
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

	// THE VOID'S CITIES DO NOT COME BACK. `AddVoidStacks` marks a city doomed
	// at its tenth Void stack and then calls this; see `FCataclysmCity::bDoomed`.
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
	City->Defence = EffectiveMaxDefence(*City) * RetakenFraction;
	City->Population = EffectiveMaxPopulation(*City) * RetakenFraction;

	// A RETAKEN CITY'S VOID STACKS ARE CLEARED, as `Simulation._retake` clears
	// them, ruled by the coordinating session on 2026-10-16. It fell with fewer
	// than ten, or it would have been erased and refused above.
	City->VoidStacks = 0;

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
