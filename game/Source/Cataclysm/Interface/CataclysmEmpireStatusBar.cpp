// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireStatusBar.h"

#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "HAL/IConsoleManager.h"
#include "Interface/CataclysmCreaturePanel.h"
#include "Player/CataclysmGameInstance.h"

// Warm near-white, as six-digit sRGB hex for the reason `CataclysmCombatOverlay.cpp` gives. The rarity word's ink.
const TCHAR* UCataclysmEmpireStatusBar::TextHex = TEXT("F5F0EA");

namespace
{
	/**
	 * Whether the empire status bar is drawn.
	 *
	 * ITS OWN SWITCH, beside the overlay's, so a player recording a fight can take it off without losing the bars or
	 * the numbers, and the other way round.
	 */
	TAutoConsoleVariable<int32> CVarShowEmpireStatusBar(
		TEXT("Cataclysm.Overlay.EmpireStatusBar"),
		1,
		TEXT("Whether the empire status bar -- dungeons standing, the next surge, cities at risk -- is drawn in the top "
			 "left corner. 1 draws it, 0 does not. Nothing is drawn while no empire run is bound, whatever this is set "
			 "to."),
		ECVF_Default);
}

bool UCataclysmEmpireStatusBar::Enabled()
{
	return CVarShowEmpireStatusBar.GetValueOnAnyThread() != 0;
}

// ---------------------------------------------------------------------------
// Gathering the figures from a run
// ---------------------------------------------------------------------------

int32 UCataclysmEmpireStatusBar::CitiesAtRiskIn(const UCataclysmEmpireRun* Run)
{
	if (Run == nullptr || Run->Map == nullptr)
	{
		return 0;
	}

	// A SET, SO A CITY CARRYING SEVERAL DUNGEONS COUNTS ONCE. Ruling (a): the bar counts cities, and three dungeons on
	// one Outpost put one city at risk.
	TSet<int32> AtRisk;

	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		// A FALLEN CITY HAS NOTHING LEFT TO LOSE, though a Fallen City dungeon stands on it from the day it falls.
		const FCataclysmCity* City = Run->Map->Find(Dungeon.CityId);
		if (City != nullptr && !City->bFallen)
		{
			AtRisk.Add(Dungeon.CityId);
		}
	}

	return AtRisk.Num();
}

int32 UCataclysmEmpireStatusBar::DaysToNextSurgeIn(const UCataclysmEmpireRun* Run)
{
	if (Run == nullptr || Run->Surges == nullptr)
	{
		return 0;
	}

	// FROM THE DAY THE WALK HAS REACHED, ruling (b). The day the run shows does not move during a walk -- its days are
	// owed and run when the player leaves -- so counting from `Day()` alone would hold the countdown still while the
	// player walks floor after floor towards the surge.
	const float Days = Run->Surges->DaysUntilNextSurge(Run->Day() + Run->DaysOwed());

	// ROUNDED UP: a surge 10.5 days off lands on the eleventh day, the first whole day at or after it.
	return FMath::Max(0, FMath::CeilToInt(Days));
}

FCataclysmEmpireStatusFacts UCataclysmEmpireStatusBar::FactsOf(const UCataclysmEmpireRun* Run)
{
	FCataclysmEmpireStatusFacts Facts;

	// A RUN THAT HAS NOT BEGUN IS NOT A RUN TO SHOW. `Begin` is what builds the map, the clock and the scheduler, so a
	// run object without them is the same case as no run, as `UCataclysmEmpireMapLayout::ProgressLine` treats it.
	if (Run == nullptr || Run->Map == nullptr || Run->Clock == nullptr || Run->Surges == nullptr)
	{
		return Facts;
	}

	Facts.bRunBound = true;
	Facts.DungeonsStanding = Run->DungeonCount();
	Facts.DaysToNextSurge = DaysToNextSurgeIn(Run);
	Facts.DaysOwed = Run->DaysOwed();
	Facts.CitiesAtRisk = CitiesAtRiskIn(Run);
	return Facts;
}

FCataclysmEmpireStatusFacts UCataclysmEmpireStatusBar::FactsForWorld(const UObject* WorldContext)
{
	// NEVER STARTING ONE. A player pressing Play in `L_Dungeon` to look at a floor is not beginning a campaign, for the
	// reason `ACataclysmDungeonGameMode::EmpireRun` gives.
	return FactsOf(UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false));
}

// ---------------------------------------------------------------------------
// What the line says
// ---------------------------------------------------------------------------

FString UCataclysmEmpireStatusBar::LineFor(const FCataclysmEmpireStatusFacts& Facts)
{
	if (!Facts.bRunBound)
	{
		return FString();
	}

	const FString Dungeons = FString::Printf(TEXT("%d %s standing"), Facts.DungeonsStanding,
											 Facts.DungeonsStanding == 1 ? TEXT("dungeon") : TEXT("dungeons"));

	FString Surge;
	if (Facts.DaysToNextSurge <= 0 && Facts.DaysOwed > 0)
	{
		// THE DAYS OWED REACH IT, and they run when the player leaves.
		Surge = TEXT("surge when you leave");
	}
	else if (Facts.DaysToNextSurge <= 1)
	{
		// ONE DAY OFF, OR ALREADY DUE WITH NOTHING OWED: either way it lands as the next day begins.
		Surge = TEXT("surge tomorrow");
	}
	else
	{
		Surge = FString::Printf(TEXT("surge in %d days"), Facts.DaysToNextSurge);
	}

	const FString Cities = FString::Printf(TEXT("%d %s at risk"), Facts.CitiesAtRisk,
										   Facts.CitiesAtRisk == 1 ? TEXT("city") : TEXT("cities"));

	return FString::Printf(TEXT("%s | %s | %s"), *Dungeons, *Surge, *Cities);
}

FString UCataclysmEmpireStatusBar::LineForWorld(const UObject* WorldContext)
{
	return LineFor(FactsForWorld(WorldContext));
}

// ---------------------------------------------------------------------------
// Where it goes
// ---------------------------------------------------------------------------

float UCataclysmEmpireStatusBar::RoomBesideThePanel(float ViewportWidth)
{
	const float PanelLeft = ViewportWidth * (1.0f - UCataclysmCreaturePanel::MinimumWidthShare) * 0.5f;
	return PanelLeft - GapToPanelPx - LeftPx;
}

float UCataclysmEmpireStatusBar::ScaleToFit(float WidthAtBaseScale, float ViewportWidth)
{
	if (WidthAtBaseScale <= 0.0f)
	{
		return BaseScale;
	}

	const float Room = RoomBesideThePanel(ViewportWidth);
	if (Room <= 0.0f)
	{
		return SmallestScale;
	}

	// TEXT WIDTH IS LINEAR IN ITS SCALE, so the scale that fits is the base scale times the share of the line that fits.
	return FMath::Clamp(BaseScale * Room / WidthAtBaseScale, SmallestScale, BaseScale);
}
