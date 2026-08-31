// Copyright Stephen Dubois. All Rights Reserved.

#include "DayClock/CataclysmDayClock.h"

int32 UCataclysmDayClock::RunDaysFor(int32 Floors)
{
	// ROUNDED UP FIRST AND CLAMPED SECOND, the order `Simulation.run_days_for`
	// uses. With one day a floor the ceiling changes nothing today, and it is
	// here because `DaysPerFloor` is a float and the model rounds up: a rate that
	// ever stopped being exactly 1.0 would otherwise silently truncate.
	const float Days = FMath::Max(0, Floors) * DaysPerFloor;

	return FMath::Clamp(FMath::CeilToInt(Days), LeastRunDays, MostRunDays);
}

float UCataclysmDayClock::ResolveDaysFor(int32 Floors)
{
	return ResolveBaseDays + FMath::Max(0, Floors) * ResolveFloorRatio;
}

int32 UCataclysmDayClock::DeathDayCostFor(int32 LethalityRung)
{
	switch (LethalityRung)
	{
	case 1:		return DeathDayCostHardcore;
	case 2:		return DeathDayCostHeretic;
	default:	return DeathDayCostStandard;
	}
}

bool UCataclysmDayClock::AddDungeon(int32 DungeonId, int32 Floors)
{
	if (FindTimer(DungeonId) != nullptr)
	{
		// A SECOND TIMER FOR ONE DUNGEON WOULD BITE THE SAME CITY TWICE. Refusing
		// is the honest answer; nothing here is in a position to decide which of
		// the two the caller meant.
		return false;
	}

	FCataclysmDungeonTimer Timer;
	Timer.DungeonId = DungeonId;
	Timer.Floors = FMath::Max(1, Floors);
	Timer.ResolveDays = ResolveDaysFor(Timer.Floors);
	Timer.DaysUntilResolve = Timer.ResolveDays;

	Timers.Add(Timer);
	return true;
}

bool UCataclysmDayClock::SetResolveDays(int32 DungeonId, float Days)
{
	if (Days < 0.0f)
	{
		// A DUNGEON THAT RESOLVES BEFORE IT EXISTS IS NOT A SHORTER TIMER. It is
		// a mistake somewhere above, and refusing says so where accepting would
		// bite a city on the day the dungeon arrived.
		return false;
	}

	FCataclysmDungeonTimer* Timer = Timers.FindByPredicate(
		[DungeonId](const FCataclysmDungeonTimer& Candidate)
		{
			return Candidate.DungeonId == DungeonId;
		});

	if (Timer == nullptr)
	{
		return false;
	}

	// BOTH, AND NOT ONLY THE ONE COUNTING DOWN. See the header: a dungeon that
	// resolved and then refilled to a different figure than it started with
	// would behave as two different dungeons under one number.
	Timer->ResolveDays = Days;
	Timer->DaysUntilResolve = Days;

	return true;
}

const FCataclysmDungeonTimer* UCataclysmDayClock::FindTimer(int32 DungeonId) const
{
	return Timers.FindByPredicate(
		[DungeonId](const FCataclysmDungeonTimer& Timer)
		{
			return Timer.DungeonId == DungeonId;
		});
}

float UCataclysmDayClock::DaysUntilResolveFor(int32 DungeonId) const
{
	const FCataclysmDungeonTimer* Timer = FindTimer(DungeonId);
	return Timer ? Timer->DaysUntilResolve : -1.0f;
}

void UCataclysmDayClock::EnterDungeon(int32 DungeonId)
{
	CurrentDungeonId = DungeonId;
}

void UCataclysmDayClock::LeaveDungeon()
{
	CurrentDungeonId = INDEX_NONE;
}

TArray<int32> UCataclysmDayClock::AdvanceDay()
{
	TArray<int32> Resolved;

	++Day;

	// TWO PASSES, WHICH IS WHAT THE MODEL DOES AND IS NOT AN ACCIDENT OF STYLE.
	// `Simulation.step` moves every timer and only then looks at which reached
	// zero. Doing both in one pass would resolve a dungeon while later dungeons
	// in the list had not yet had today taken off them, which matters the moment
	// a resolve does anything to anything else.
	for (FCataclysmDungeonTimer& Timer : Timers)
	{
		const bool bInside = CurrentDungeonId != INDEX_NONE
			&& Timer.DungeonId == CurrentDungeonId;

		if (bInside && !bTimerTicksWhileRunning)
		{
			continue;
		}

		Timer.DaysUntilResolve -= 1.0f;
	}

	for (FCataclysmDungeonTimer& Timer : Timers)
	{
		if (Timer.DaysUntilResolve > 0.0f)
		{
			continue;
		}

		++Timer.TimesResolved;
		Resolved.Add(Timer.DungeonId);

		// AND IT STAYS, WITH ITS TIMER FULL AGAIN. See
		// `bDungeonPersistsAfterResolve`: a dungeon that vanished after biting
		// once would mean an ignored city could never actually die.
		Timer.DaysUntilResolve = Timer.ResolveDays;
	}

	// REMOVED ONLY IF THE RULE SAYS SO. The branch is here rather than left out
	// because the constant it reads is one of the ones the port test compares,
	// and a constant nothing reads is a constant that can be changed with no
	// effect and no warning.
	if (!bDungeonPersistsAfterResolve)
	{
		Timers.RemoveAll([&Resolved](const FCataclysmDungeonTimer& Timer)
		{
			return Resolved.Contains(Timer.DungeonId);
		});
	}

	return Resolved;
}

TArray<int32> UCataclysmDayClock::AdvanceDays(int32 Days)
{
	TArray<int32> Resolved;

	for (int32 Passed = 0; Passed < Days; ++Passed)
	{
		Resolved.Append(AdvanceDay());
	}

	return Resolved;
}
