// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireMapLayout.h"

int32 UCataclysmEmpireMapLayout::CellsAcross()
{
	// DERIVED FROM THE MAP'S OWN RADIUS AND NOT WRITTEN DOWN AGAIN. The lattice
	// runs from -3 to 3 on each axis, so it is seven cells across; a radius that
	// ever changed would change the drawing with it rather than leaving a stale
	// seven here.
	return 2 * UCataclysmEmpireMap::Radius + 1;
}

FVector2D UCataclysmEmpireMapLayout::DiamondSize()
{
	// THE CENTRES SPAN SIX CELLS, NOT SEVEN, and then half a city hangs off each
	// end. Measuring it as seven cells would be close enough to look right and
	// would leave the diamond slightly smaller than the panel it was fitted to,
	// which is a margin nobody asked for.
	const float Cells = static_cast<float>(CellsAcross() - 1);

	return FVector2D(Cells * CellWidthPx + CityWidthPx,
					 Cells * CellHeightPx + CityHeightPx);
}

float UCataclysmEmpireMapLayout::ScaleToFit(FVector2D PanelSize)
{
	const FVector2D Diamond = DiamondSize();
	if (Diamond.X <= 0.0 || Diamond.Y <= 0.0)
	{
		return SmallestScale;
	}

	const double Room = 2.0 * FitMarginPx;
	const double Wide = PanelSize.X - Room;
	const double Tall = PanelSize.Y - Room;

	if (Wide <= 0.0 || Tall <= 0.0)
	{
		// A PANEL SMALLER THAN ITS OWN MARGINS. It happens while a screen is
		// being laid out and before Slate has given it a size, and the smallest
		// scale is a poor drawing rather than a division by nothing.
		return SmallestScale;
	}

	return ClampScale(static_cast<float>(
		FMath::Min(Wide / Diamond.X, Tall / Diamond.Y)));
}

FVector2D UCataclysmEmpireMapLayout::PositionFor(int32 R, int32 C, float Scale,
												 FVector2D PanelSize)
{
	const FVector2D Centre = PanelSize * 0.5;

	// THE ROW GROWS DOWNWARDS AND THE COLUMN RIGHTWARDS, which is what turns the
	// taxicab ball into the diamond the design document draws: the Pillar at
	// (0,0) lands in the middle, and a cell at (-3,0) lands three rows above it.
	return Centre + FVector2D(C * CellWidthPx * Scale, R * CellHeightPx * Scale);
}

FVector2D UCataclysmEmpireMapLayout::CitySize(float Scale)
{
	return FVector2D(CityWidthPx * Scale, CityHeightPx * Scale);
}

float UCataclysmEmpireMapLayout::ClampScale(float Scale)
{
	return FMath::Clamp(Scale, SmallestScale, LargestScale);
}

ECataclysmCityMark UCataclysmEmpireMapLayout::MarkFor(
	const UCataclysmEmpireMap* Map, int32 CityId)
{
	if (Map == nullptr)
	{
		return ECataclysmCityMark::Sealed;
	}

	const FCataclysmCity* City = Map->Find(CityId);
	if (City == nullptr)
	{
		return ECataclysmCityMark::Sealed;
	}

	if (City->bFallen)
	{
		return ECataclysmCityMark::Fallen;
	}

	// THE MAP'S OWN ANSWER. `IsExposed` is what decides where a surge can land,
	// so asking anything else here would show the player a rule the game does
	// not obey.
	return Map->IsExposed(CityId) ? ECataclysmCityMark::Exposed
								  : ECataclysmCityMark::Sealed;
}

FString UCataclysmEmpireMapLayout::CityLabel(const UCataclysmEmpireMap* Map,
											 int32 CityId)
{
	if (Map == nullptr)
	{
		return FString();
	}

	const FCataclysmCity* City = Map->Find(CityId);
	if (City == nullptr)
	{
		return FString();
	}

	const FString Tier = UCataclysmEmpireMap::TierName(City->Tier);

	if (City->bFallen)
	{
		return FString::Printf(TEXT("%s lost"), *Tier);
	}

	// ROUNDED UP, SO A CITY WITH ANYTHING LEFT NEVER READS AS 0%. A player told
	// a city is at nothing would stop defending it, and a city at 0.4% is still
	// standing and still savable.
	const int32 Percent = FMath::CeilToInt(City->DefenceFraction() * 100.0f);

	return FString::Printf(TEXT("%s %d%%"), *Tier, FMath::Clamp(Percent, 0, 100));
}
