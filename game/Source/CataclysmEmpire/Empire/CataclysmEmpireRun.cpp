// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmEmpireRun.h"

// ---------------------------------------------------------------------------
// Starting a run
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::Begin(int32 InSeed, ECataclysmSurgeMode Mode,
								int32 LethalityRung)
{
	Map = NewObject<UCataclysmEmpireMap>(this);
	Map->Build();

	Clock = NewObject<UCataclysmDayClock>(this);

	Surges = NewObject<UCataclysmSurgeScheduler>(this);
	Surges->Mode = Mode;
	Surges->LethalityRung = LethalityRung;

	Dungeons.Reset();
	NextDungeonId = 0;

	Stream.Initialize(InSeed);
}

int32 UCataclysmEmpireRun::Day() const
{
	return Clock ? Clock->Day : 0;
}

bool UCataclysmEmpireRun::IsLost() const
{
	return Map != nullptr && Map->IsPillarExposed();
}

// ---------------------------------------------------------------------------
// A day
// ---------------------------------------------------------------------------

FCataclysmDayReport UCataclysmEmpireRun::AdvanceDay()
{
	FCataclysmDayReport Report;

	if (Map == nullptr || Clock == nullptr || Surges == nullptr)
	{
		// `Begin` HAS NOT RUN. Answering an empty report is better than reading
		// through a null map, and a caller that never called `Begin` will see a
		// day that never advances rather than a crash.
		return Report;
	}

	// THE DAY THAT IS ABOUT TO BEGIN. `UCataclysmDayClock::AdvanceDay` moves the
	// day on and the timers in one call, and the surge has to land between those
	// two -- `Simulation.step` increments the day, fires the surge, and only then
	// moves the timers. So today's number has to be known before the clock is
	// asked to move.
	//
	// A TEST PINS THIS. `Cataclysm.EmpireRun.TheReportedDayIsTheClocksDay` fails
	// if the clock ever stops advancing by exactly one.
	const int32 Today = Clock->Day + 1;
	Report.Day = Today;

	if (Surges->IsDue(Today))
	{
		FireSurge(Today, /* bFromCityFall */ false, Report);
	}

	// EVERY TIMER MOVES, INCLUDING ONE THAT ARRIVED A MOMENT AGO. That is what
	// the model does: a dungeon spawned today has a day taken off it today.
	const TArray<int32> Resolved = Clock->AdvanceDay();

	Report.Resolved = Resolved;

	for (const int32 DungeonId : Resolved)
	{
		ResolveDungeon(DungeonId, Report);
	}

	Report.bPillarExposed = Map->IsPillarExposed();

	return Report;
}

TArray<FCataclysmDayReport> UCataclysmEmpireRun::AdvanceDays(int32 Days)
{
	TArray<FCataclysmDayReport> Reports;

	for (int32 Passed = 0; Passed < Days; ++Passed)
	{
		Reports.Add(AdvanceDay());
	}

	return Reports;
}

// ---------------------------------------------------------------------------
// A surge
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::FireSurge(int32 Today, bool bFromCityFall,
									FCataclysmDayReport& OutReport)
{
	const TArray<FCataclysmDungeon> Wave =
		Surges->RollWave(*Map, Today, NextDungeonId, Stream);

	for (const FCataclysmDungeon& Dungeon : Wave)
	{
		// THE CLOCK IS TOLD THE DEPTH AND WORKS OUT ITS OWN TIMER, and then the
		// rolled one replaces it. The clock's figure has no jitter in it, and
		// two dungeons of one depth coming due on the same day is exactly what
		// the jitter exists to prevent -- so the roll wins.
		if (!Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors))
		{
			// A dungeon of that number is already counting down. It cannot
			// happen while `NextDungeonId` only counts up, and dropping the
			// dungeon is better than adding one the clock will never move.
			continue;
		}

		Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays);

		Dungeons.Add(Dungeon);
		OutReport.Spawned.Add(Dungeon.DungeonId);

		NextDungeonId = FMath::Max(NextDungeonId, Dungeon.DungeonId + 1);
	}

	// THE SCHEDULE MOVES WHETHER OR NOT ANYTHING LANDED. A wave that found no
	// target still counts as the surge for that period, which is what
	// `Simulation.trigger_surge` does, and not moving it would fire a surge
	// every day for ever.
	Surges->RecordSurge(Today, bFromCityFall);

	OutReport.bSurged = true;
}

// ---------------------------------------------------------------------------
// A dungeon resolving, and what it costs
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::ResolveDungeon(int32 DungeonId,
										 FCataclysmDayReport& OutReport)
{
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	if (Dungeon == nullptr)
	{
		return;
	}

	const int32 CityId = Dungeon->CityId;

	const FCataclysmCity* City = Map->Find(CityId);
	if (City == nullptr || City->bFallen)
	{
		// NOTHING LEFT TO BITE. The model refreshes the timer and moves on, and
		// the clock has already refreshed it.
		return;
	}

	// A DEEPER DUNGEON HITS HARDER, in proportion to how deep it is against a
	// typical one of its kind on that tier of city.
	const float Scale = Dungeon->BiteScale();
	const float Defence = Dungeon->DefenceBite * Scale;
	const float Population = Dungeon->PopulationBite * Scale;

	// COPIED OUT BEFORE THE BITE. `Bite` can lead to `CityFell`, which removes
	// dungeons from `Dungeons`, and `Dungeon` points into that array.
	if (Map->Bite(CityId, Defence, Population))
	{
		CityFell(CityId, OutReport);
	}
}

void UCataclysmEmpireRun::CityFell(int32 CityId, FCataclysmDayReport& OutReport)
{
	OutReport.Fallen.Add(CityId);

	// EVERY DUNGEON STANDING ON IT IS ABSORBED. In the design they become the
	// Dungeon City the fallen city turns into, and how many were there sets its
	// floor count. Nothing builds that yet, so they are removed and counted.
	// Issue #41.
	const TArray<int32> Standing = DungeonsOn(CityId);

	for (const int32 DungeonId : Standing)
	{
		OutReport.Absorbed.Add(DungeonId);
		ClearDungeon(DungeonId);
	}

	// AND THE FALL IS ITSELF A SURGE. The design document says a city falling
	// triggers one; `UCataclysmSurgeScheduler::bCityFallAdvancesEscalation` is
	// what makes it also speed the rest of the run up.
	if (UCataclysmSurgeScheduler::bSurgeOnCityFall)
	{
		FireSurge(OutReport.Day, /* bFromCityFall */ true, OutReport);
	}
}

// ---------------------------------------------------------------------------
// The dungeons standing on the map
// ---------------------------------------------------------------------------

const FCataclysmDungeon* UCataclysmEmpireRun::FindDungeon(int32 DungeonId) const
{
	return Dungeons.FindByPredicate(
		[DungeonId](const FCataclysmDungeon& Candidate)
		{
			return Candidate.DungeonId == DungeonId;
		});
}

TArray<int32> UCataclysmEmpireRun::DungeonsOn(int32 CityId) const
{
	TArray<int32> Standing;

	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		if (Dungeon.CityId == CityId)
		{
			Standing.Add(Dungeon.DungeonId);
		}
	}

	return Standing;
}

bool UCataclysmEmpireRun::ClearDungeon(int32 DungeonId)
{
	const int32 Removed = Dungeons.RemoveAll(
		[DungeonId](const FCataclysmDungeon& Candidate)
		{
			return Candidate.DungeonId == DungeonId;
		});

	if (Removed == 0)
	{
		return false;
	}

	// AND ITS TIMER GOES WITH IT. The two lists are kept in step here and
	// nowhere else; a dungeon left on the clock would keep biting a city that no
	// longer has a dungeon on it.
	if (Clock != nullptr)
	{
		Clock->Timers.RemoveAll(
			[DungeonId](const FCataclysmDungeonTimer& Timer)
			{
				return Timer.DungeonId == DungeonId;
			});

		if (Clock->CurrentDungeonId == DungeonId)
		{
			Clock->LeaveDungeon();
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Seeing it
// ---------------------------------------------------------------------------

FString UCataclysmEmpireRun::Describe() const
{
	if (Map == nullptr || Clock == nullptr || Surges == nullptr)
	{
		return TEXT("The run has not begun.");
	}

	TArray<FString> Lines;

	Lines.Add(FString::Printf(
		TEXT("Day %d. %d dungeons standing. %d cities lost of 25."),
		Clock->Day, Dungeons.Num(), Map->FallenCityCount()));

	Lines.Add(FString::Printf(
		TEXT("%d cities from defeat. Next surge in %.0f days, bringing %d."),
		Map->DistanceToDefeat(), Surges->DaysUntilNextSurge(Clock->Day),
		Surges->DungeonsInNextSurge()));

	if (Map->IsPillarExposed())
	{
		Lines.Add(TEXT("A Sanctuary has fallen. The Cataclysm can reach the "
					   "Pillar."));
	}

	Lines.Add(TEXT(""));
	Lines.Add(Map->Render());

	if (!Dungeons.IsEmpty())
	{
		Lines.Add(TEXT(""));

		for (const FCataclysmDungeon& Dungeon : Dungeons)
		{
			const FCataclysmCity* City = Map->Find(Dungeon.CityId);

			Lines.Add(FString::Printf(
				TEXT("  dungeon %d on %s: %d floors, %.1f days until it "
					 "resolves"),
				Dungeon.DungeonId,
				City ? *City->Name : TEXT("nowhere"),
				Dungeon.Floors,
				Clock->DaysUntilResolveFor(Dungeon.DungeonId)));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}
