// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmEmpireRun.h"

// ---------------------------------------------------------------------------
// Starting a run
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::Begin(int32 InSeed, ECataclysmSurgeMode Mode,
								int32 LethalityRung, int32 DifficultyTier)
{
	Map = NewObject<UCataclysmEmpireMap>(this);
	Map->Build();

	// HERETIC GIVES A CITY TWO UPGRADE SLOTS INSTEAD OF THREE. The map cannot
	// work that out for itself: the lethality rung belongs to the run, and a map
	// built on its own keeps the ordinary three.
	Map->UpgradeSlots = UCataclysmCityUpgradeRules::SlotsFor(LethalityRung);

	Clock = NewObject<UCataclysmDayClock>(this);

	Surges = NewObject<UCataclysmSurgeScheduler>(this);
	Surges->Mode = Mode;
	Surges->LethalityRung = LethalityRung;

	Dungeons.Reset();
	NextDungeonId = 0;

	// A SECOND `Begin` IS A FRESH RUN, so what the last one achieved goes with
	// it. Leaving these standing would carry one campaign's quest objectives
	// into the next, and the win condition slice 6 builds reads them. Issue
	// #1324 slice 5.
	DungeonsCleared = 0;
	BasicDungeonsCleared = 0;
	QuestObjectives = 0;
	DungeonsDetonated = 0;

	Stream.Initialize(InSeed);

	// WHICH CATACLYSMS THIS CHARACTER FACES. How many comes from the difficulty
	// tier and which ones from the seed, so replaying the same seed meets the
	// same Cataclysms and the same seed one tier higher meets those plus one --
	// the project owner's ruling of 2026-09-06. Issues #1338 and #1357.
	ActiveCataclysms = UCataclysmRoster::ActiveFor(InSeed, DifficultyTier);

	// EVERY ACTIVE CATACLYSM IS A KEY BEFORE ANYTHING HAS HAPPENED, so one that
	// has never sent a quest dungeon reads as 0 rather than as absent. A caller
	// showing the player their progress towards each Cataclysm gets the whole
	// campaign rather than only the parts of it that have started.
	QuestObjectivesByCataclysm.Reset();
	for (const ECataclysmType Cataclysm : ActiveCataclysms)
	{
		QuestObjectivesByCataclysm.Add(Cataclysm, 0);
	}

	// ITS OWN STREAM, AND NOT THE SAME SEED SHIFTED BY ONE. See `CataclysmStream`
	// for why it is separate from `Stream`, and `UCataclysmRoster::MixedSeed` for
	// why deriving it by adding to the seed would have made one run's Cataclysm
	// draws a copy of the next run's waves.
	CataclysmStream.Initialize(
		UCataclysmRoster::MixedSeed(InSeed, UCataclysmRoster::WaveSalt));
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
// The Cataclysm dungeon's unlock -- issue #1357
// ---------------------------------------------------------------------------

ECataclysmType UCataclysmEmpireRun::RollCataclysm()
{
	if (ActiveCataclysms.Num() == 0)
	{
		return ECataclysmType::None;
	}

	// INCLUSIVE AT BOTH ENDS, which is what `FRandomStream::RandRange` is, so
	// the last entry can be drawn.
	return ActiveCataclysms[
		CataclysmStream.RandRange(0, ActiveCataclysms.Num() - 1)];
}

int32 UCataclysmEmpireRun::QuestObjectivesFor(ECataclysmType Cataclysm) const
{
	const int32* Earned = QuestObjectivesByCataclysm.Find(Cataclysm);
	return Earned != nullptr ? *Earned : 0;
}

bool UCataclysmEmpireRun::IsCataclysmComplete(ECataclysmType Cataclysm) const
{
	// `None` CAN NEVER BE COMPLETE. It asks for nothing, and "nothing" is met by
	// nothing, so without this a caller asking about it would be told yes. It is
	// never in `ActiveCataclysms` so the rule below never asks -- but this is
	// public and a caller can ask anything.
	if (Cataclysm == ECataclysmType::None)
	{
		return false;
	}

	return QuestObjectivesFor(Cataclysm)
		>= UCataclysmRoster::QuestObjectivesFor(Cataclysm);
}

int32 UCataclysmEmpireRun::CataclysmsComplete() const
{
	int32 Complete = 0;

	// OVER THE ACTIVE SET AND NOT OVER THE MAP OF TALLIES. They hold the same
	// keys today, because `Begin` seeds one from the other and `ClearDungeon`
	// only ever raises a Cataclysm that sent a dungeon -- which can only be an
	// active one. Counting the active set is what the rule is about, so a tally
	// that somehow named a Cataclysm this run does not face could not make the
	// player's requirement easier to meet.
	for (const ECataclysmType Cataclysm : ActiveCataclysms)
	{
		if (IsCataclysmComplete(Cataclysm))
		{
			++Complete;
		}
	}

	return Complete;
}

int32 UCataclysmEmpireRun::CataclysmDungeonRequirement() const
{
	return UCataclysmRoster::CataclysmsRequiredFor(ActiveCataclysms.Num());
}

bool UCataclysmEmpireRun::IsCataclysmDungeonUnlocked() const
{
	// A RUN THAT NEVER BEGAN IS NOT UNLOCKED. `CataclysmsRequiredFor` clamps an
	// empty list up to a requirement of one, so the comparison below already
	// answers false -- but only by arithmetic, and saying it here means a change
	// to that clamp cannot quietly open the boss on a run with no Cataclysms.
	if (ActiveCataclysms.Num() == 0)
	{
		return false;
	}

	return CataclysmsComplete() >= CataclysmDungeonRequirement();
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

	// A CITY REPAIRS ITSELF BEFORE THE DAY'S ASSAULTS LAND, not after. Both
	// orders give the same figure on an ordinary day, because a heal and a bite
	// commute. They differ on the one day that matters: a city one bite from
	// falling is saved by a repair that was already due, rather than falling and
	// then being healed while fallen. Repairing first is the reading a player
	// would expect and the more forgiving of the two.
	RunCityUpgradeIntervals(Today);

	// EVERY TIMER MOVES, INCLUDING ONE THAT ARRIVED A MOMENT AGO. That is what
	// the model does: a dungeon spawned today has a day taken off it today.
	const TArray<int32> Resolved = Clock->AdvanceDay();

	Report.Resolved = Resolved;

	// A SIEGE TAKES ITS SHARE BEFORE ANY TIMER IS ACTED ON, so the day's two
	// kinds of assault land together and after the repairs above. A city felled
	// here loses its dungeons with it, and `ResolveDungeon` below finds nothing
	// to resolve for them, which is the right answer: a city cannot be bitten
	// twice for falling once.
	ApplySiegeDamage(Report);

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

TArray<FCataclysmDayReport> UCataclysmEmpireRun::SpendFloorTime(float Days)
{
	TArray<FCataclysmDayReport> Reports;

	if (Clock == nullptr)
	{
		return Reports;
	}

	// THE CLOCK HOLDS THE PART OF A DAY AND THIS SPENDS THE WHOLE ONES. Every
	// whole day goes through `AdvanceDay` below, so a day that accumulated out
	// of twenty-five floors fires its surge, repairs its cities and resolves its
	// dungeons exactly as a day spent in one go would.
	while (Clock->TakeAWholeDay(Days))
	{
		Reports.Add(AdvanceDay());

		// THE COST IS ADDED ONCE. Asking again with zero drains whatever is left
		// in the carry -- a floor costing three days would otherwise leave two
		// of them unspent until the next floor.
		Days = 0.0f;
	}

	return Reports;
}

// ---------------------------------------------------------------------------
// A surge
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::FireSurge(int32 Today, bool bFromCityFall,
									FCataclysmDayReport& OutReport)
{
	// HOW MANY DUNGEONS EACH CITY ALREADY HOLDS, because the surge scheduler
	// needs it for the dungeon cap upgrade and does not own the dungeons. Passed
	// in rather than reached for: `UCataclysmSurgeScheduler` is deliberately
	// ignorant of this list, which is what keeps it able to roll a wave against
	// a bare map in a test.
	TArray<int32> DungeonsPerCity;
	DungeonsPerCity.AddZeroed(Map->Cities.Num());

	// AND HOW MANY OF THEM ARE SIEGES, counted separately and for a different
	// rule: the cap above is an upgrade a city may buy, this one is the design's
	// "Max 1 per city" and applies to every city whether it bought anything or
	// not. Passed in for the same reason -- the scheduler does not own the
	// dungeons and must stay able to roll a wave against a bare map in a test.
	TArray<int32> SiegesStanding;
	SiegesStanding.AddZeroed(Map->Cities.Num());

	for (const FCataclysmDungeon& Standing : Dungeons)
	{
		if (DungeonsPerCity.IsValidIndex(Standing.CityId))
		{
			++DungeonsPerCity[Standing.CityId];
		}

		if (Standing.SubType == ECataclysmDungeonSubType::Siege
			&& SiegesStanding.IsValidIndex(Standing.CityId))
		{
			++SiegesStanding[Standing.CityId];
		}
	}

	TArray<FCataclysmDungeon> Wave =
		Surges->RollWave(*Map, Today, NextDungeonId, Stream, DungeonsPerCity,
						 SiegesStanding);

	// AND EACH ONE IS STAMPED WITH THE CATACLYSM THAT SENT IT. Issue #1357.
	//
	// HERE AND NOT IN THE SCHEDULER, which is where the model puts it too:
	// `Simulation.trigger_surge` sets `d.source` after `_make_dungeon` has
	// returned. The scheduler is deliberately ignorant of which Cataclysms this
	// campaign faces -- it must stay able to roll a wave against a bare map in a
	// test, the same reason the dungeon and siege counts are passed in -- and
	// the active set belongs to the run.
	//
	// BEFORE THE CLOCK IS TOLD ANYTHING, so a dungeon cannot reach `Dungeons`
	// carrying `None` and be corrected afterwards. There is no correcting it:
	// `ClearDungeon` reads this field to decide which Cataclysm an objective
	// counts for, and a dungeon that reached the map unstamped would earn the
	// player nothing when they cleared it.
	for (FCataclysmDungeon& Dungeon : Wave)
	{
		Dungeon.Cataclysm = RollCataclysm();
	}

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

	if (!Dungeon->Resolves())
	{
		// IT REFRESHES RATHER THAN DETONATING, and the clock has already
		// refreshed it -- see `UCataclysmDayClock::AdvanceDay`, which sets every
		// timer that ran out back to full. So nothing here is taken from the
		// city.
		//
		// A QUEST DUNGEON IS THE ONE THIS IS ABOUT IN PLAY.
		// `docs/Cataclysm_GDD_v2.md` section VIII: it "does not resolve --
		// refreshes and may move to adjacent city". The timer is a relocation
		// clock and it is MEANT to run out; what must not happen is the city
		// paying for it. Issue #1324 slice 3, and the move below is slice 4.
		//
		// A FALLEN CITY AND A CATACLYSM REACH THIS TOO, and would have been
		// stopped by the fallen-city check below in any case -- a Fallen City
		// stands on a city that has by definition fallen. This is the first of
		// the two reasons rather than the only one, and it is the one that
		// states the design instead of relying on a coincidence.
		RelocateQuestDungeon(DungeonId, OutReport);
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
	//
	// POINTS AND NOT A SHARE OF THE CITY, WHICH IS ISSUE #1331. `Map->Damage`
	// and not `Map->Bite`: the city's own maximum is deliberately not a factor
	// here, so raising a city's ceiling raises how many resolves it survives.
	const float Scale = Dungeon->BiteScale();
	const float Defence = Dungeon->DefenceDamage * Scale;
	const float Population = Dungeon->PopulationDamage * Scale;

	// THE EMPIRE IS ABOUT TO PAY, SO THIS IS WHERE IT IS COUNTED. Every return
	// above this line leaves the city untouched -- a kind that does not
	// detonate, or a host that has already fallen -- so a tally raised at the
	// top of this function would count timers rather than damage, which is what
	// `FCataclysmDayReport::Resolved` already gives a caller and the reason
	// nothing could answer how often the empire was hurt. Issue #1324 slice 5.
	//
	// THE MODEL COUNTS THIS DIFFERENTLY AND DELIBERATELY IS NOT COPIED.
	// `Simulation._resolve` raises `self.resolved` above its own equivalent
	// guard, so a Fallen City dungeon's timer counts there; measured at 15 of
	// 4,051 over thirty campaigns. Issue #1373.
	++DungeonsDetonated;

	// COPIED OUT BEFORE THE BITE. `Damage` can lead to `CityFell`, which removes
	// dungeons from `Dungeons`, and `Dungeon` points into that array.
	if (Map->Damage(CityId, Defence, Population))
	{
		CityFell(CityId, OutReport);
	}
}

void UCataclysmEmpireRun::RelocateQuestDungeon(int32 DungeonId,
											   FCataclysmDayReport& OutReport)
{
	if (Map == nullptr)
	{
		return;
	}

	// LOOKED UP MUTABLY AND ONLY HERE. `ResolveDungeon` holds its dungeon by
	// const pointer on purpose, so that the one function able to move a dungeon
	// is this one and a reader can find it by name.
	FCataclysmDungeon* Dungeon = Dungeons.FindByPredicate(
		[DungeonId](const FCataclysmDungeon& Candidate)
		{
			return Candidate.DungeonId == DungeonId;
		});

	if (Dungeon == nullptr)
	{
		return;
	}

	// ONLY A QUEST DUNGEON MOVES. A Fallen City and a Cataclysm also answer
	// false to `Resolves`, and both reach the caller above, but neither
	// wanders: a Fallen City IS its city, and the Cataclysm boss dungeon's one
	// move is the Last Stand, issue #43. Checking the kind here rather than at
	// the call site keeps the rule beside the thing it is a rule about.
	if (Dungeon->Type != ECataclysmDungeonType::Quest)
	{
		return;
	}

	const FCataclysmCity* From = Map->Find(Dungeon->CityId);
	if (From == nullptr || From->bFallen)
	{
		// ITS CITY FELL UNDERNEATH IT. In practice the dungeon is already gone
		// -- `CityFell` absorbs everything standing on the city -- so this is
		// belt and braces rather than a case seen in play. A dungeon standing
		// on a ruin has no territory to drift through.
		return;
	}

	// **WHAT A DUNGEON CARRYING A SIEGE MAY NOT WALK ONTO.** The design's Siege
	// row says "Max 1 per city". `UCataclysmSurgeScheduler::RollSubType`
	// enforced that when a dungeon was created and nothing enforced it when one
	// moved, so a Quest dungeon that had rolled Siege could land on a besieged
	// city and the city would take the daily bite twice. The project owner ruled
	// on 2026-09-06, verbatim "Check the limit on arrival too". Issue #1371.
	//
	// COUNTED HERE AND PASSED IN, exactly as `FireSurge` does for the spawn half
	// of the same rule. `UCataclysmSurgeScheduler` does not own the dungeons and
	// must stay able to answer against a bare map in a test.
	//
	// **THE MOVING DUNGEON IS IN THIS COUNT AND IT DOES NOT MATTER.** It is
	// standing on `From`, and a city is never among its own neighbours, so its
	// own Siege can never be the one that refuses it a destination.
	//
	// ONLY BUILT WHEN IT IS NEEDED. Every other dungeon walks past a besieged
	// city freely -- the cap is "max 1 Siege per city", not "one dungeon per
	// besieged city" -- so an empty array is passed for them, which is the
	// "caller did not say" convention `RollWave` uses.
	const bool bCarriesSiege =
		Dungeon->SubType == ECataclysmDungeonSubType::Siege;

	TArray<int32> SiegesStanding;

	if (bCarriesSiege)
	{
		SiegesStanding.AddZeroed(Map->Cities.Num());

		for (const FCataclysmDungeon& Standing : Dungeons)
		{
			if (Standing.SubType == ECataclysmDungeonSubType::Siege
				&& SiegesStanding.IsValidIndex(Standing.CityId))
			{
				++SiegesStanding[Standing.CityId];
			}
		}
	}

	const int32 MovingTo = UCataclysmSurgeScheduler::PickRelocation(
		*Map, *From, Stream, bCarriesSiege, SiegesStanding);

	if (MovingTo == INDEX_NONE)
	{
		// NOWHERE ADJACENT TO GO, SO IT STAYS, and it is deliberately NOT
		// recorded in `Relocated`. Every neighbour was sealed, fallen, the
		// Pillar, or -- for a dungeon carrying a Siege -- already besieged.
		//
		// NO COIN IS FLIPPED ON THIS PATH, and that is the port rather than an
		// optimisation. `Simulation._resolve` guards both of its draws behind
		// `if targets`, so a hemmed-in quest timer costs neither stream a
		// number; flipping here would put the two out of step for the rest of
		// the run on the first one.
		return;
	}

	// **AND NOW THE COIN.** The design says a Quest dungeon "MAY move to
	// adjacent city", and the project owner ruled on 2026-09-06 that the "may"
	// is a die roll and not only the map -- verbatim "A chance each time" --
	// then chose the number, verbatim "0.5". `QuestMoveChance` carries it and
	// the reasoning, including why balance did not choose it.
	//
	// **AFTER THE TARGET AND NOT BEFORE IT.** `Simulation._resolve` draws the
	// target first and flips second, so both numbers come off the stream
	// whenever there is somewhere to go and the two implementations consume
	// identical streams. Flipping first would save a draw on the days the
	// dungeon stays and cost the port its parity.
	//
	// IT STAYS EXACTLY AS THOUGH IT HAD NOWHERE TO GO -- not recorded in
	// `Relocated`, nothing else touched. A caller cannot tell the two apart and
	// does not need to; `Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity`
	// separates them by asking the map.
	if (Stream.FRand() >= UCataclysmSurgeScheduler::QuestMoveChance)
	{
		return;
	}

	const FCataclysmCity* To = Map->Find(MovingTo);
	if (To == nullptr)
	{
		return;
	}

	// ONLY `CityId` MOVES, AND THAT IS THE WHOLE OF THE MOVE. The project owner
	// ruled on 2026-09-06, verbatim "Keeps everything, fix the size": a
	// relocated Quest dungeon keeps its floor count, its resolve timer and its
	// sub-type, and `docs/Cataclysm_GDD_v2.md` section VIII now states all
	// three. None of them is touched here, which is what says so.
	Dungeon->CityId = MovingTo;

	// AND `CityTier` DOES NOT MOVE WITH IT, WHICH IS THE OTHER HALF OF THAT
	// RULING. This line used to read `Dungeon->CityTier = To->Tier;`.
	//
	// `CityTier` IS NOT "THE HOST'S TIER". Its own comment says what it is --
	// "that city's tier when the dungeon spawned, which set its depth" -- and
	// `BiteScale` is the only thing in the game that reads it: it divides
	// `Floors` by the midpoint of `SpecFor(Type, CityTier)`. Both halves of
	// that division have to come from the SAME specification row. Moving the
	// tier while leaving `Floors` alone made them come from different rows, so
	// a Quest dungeon that drifted inward onto a bigger city read as shallower
	// than it is and one that drifted outward read as deeper.
	//
	// IT COST NOTHING AND NOTHING WOULD HAVE FAILED WHEN IT DID. `SpecFor`
	// gives a Quest dungeon zero city damage and `Resolves` answers false for
	// it, so `BiteScale` is never reached for the one kind that can move. It
	// would have become a live wrong number, silently, the moment any non-Basic
	// kind was given city damage. `Simulation._resolve` carries the same fix and
	// the same reasoning, and
	// `Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity` is what now fails
	// if either line comes back.

	OutReport.Relocated.Add(DungeonId);
}

bool UCataclysmEmpireRun::DungeonsAgreeWithTimers(
	const TArray<FCataclysmDungeon>& Dungeons,
	const TArray<FCataclysmDungeonTimer>& Timers,
	FString& OutWhy)
{
	if (Dungeons.Num() != Timers.Num())
	{
		OutWhy = FString::Printf(
			TEXT("%d dungeons stand and %d timers count down"),
			Dungeons.Num(), Timers.Num());
		return false;
	}

	// EVERY DUNGEON HAS EXACTLY ONE TIMER. Counting rather than only looking
	// each one up, because two timers for one dungeon and none for another
	// leaves the totals equal and every dungeon findable.
	TMap<int32, int32> TimersFor;
	for (const FCataclysmDungeonTimer& Timer : Timers)
	{
		++TimersFor.FindOrAdd(Timer.DungeonId);
	}

	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		const int32* Count = TimersFor.Find(Dungeon.DungeonId);

		if (Count == nullptr)
		{
			OutWhy = FString::Printf(
				TEXT("dungeon %d has no timer, so it would never resolve"),
				Dungeon.DungeonId);
			return false;
		}

		if (*Count != 1)
		{
			OutWhy = FString::Printf(
				TEXT("dungeon %d has %d timers"), Dungeon.DungeonId, *Count);
			return false;
		}
	}

	// AND EVERY TIMER HAS A DUNGEON. The totals being equal and every dungeon
	// having one does not settle this on its own: a timer for a dungeon that is
	// not there would have been counted above and never looked for.
	for (const FCataclysmDungeonTimer& Timer : Timers)
	{
		const bool bStands = Dungeons.ContainsByPredicate(
			[&Timer](const FCataclysmDungeon& Dungeon)
			{
				return Dungeon.DungeonId == Timer.DungeonId;
			});

		if (!bStands)
		{
			OutWhy = FString::Printf(
				TEXT("a timer counts down for dungeon %d, which is not standing"),
				Timer.DungeonId);
			return false;
		}
	}

	return true;
}

bool UCataclysmEmpireRun::IsBesieged(int32 CityId) const
{
	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		if (Dungeon.SubType == ECataclysmDungeonSubType::Siege
			&& Dungeon.CityId == CityId)
		{
			return true;
		}
	}

	return false;
}

void UCataclysmEmpireRun::ApplySiegeDamage(FCataclysmDayReport& OutReport)
{
	if (Map == nullptr || Clock == nullptr)
	{
		return;
	}

	// THE CITIES AND THE DAYS COLLECTED BEFORE ANYTHING IS BITTEN, and not
	// bitten as the list is walked. `Damage` can lead to `CityFell`, which
	// removes dungeons from `Dungeons`, and walking that array while it is being
	// emptied is how a crash gets written. `ResolveDungeon` beside this takes
	// the same care for the same reason.
	TArray<TPair<int32, int32>> Hosts;

	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
		{
			// NEVER NEGATIVE. A dungeon built by hand in a test may carry a
			// spawn day later than the clock, and a negative age would heal the
			// city rather than hurt it.
			const int32 DaysStood =
				FMath::Max(0, Clock->Day - Dungeon.SpawnedDay);

			Hosts.Emplace(Dungeon.CityId, DaysStood);
		}
	}

	for (const TPair<int32, int32>& Host : Hosts)
	{
		const int32 CityId = Host.Key;

		// CHECKED AGAIN FOR EACH ONE. A city that fell to an earlier Siege in
		// this same loop must not be bitten a second time, and a city whose
		// dungeons were absorbed when it fell may no longer have the Siege that
		// put it in this list.
		const FCataclysmCity* City = Map->Find(CityId);
		if (City == nullptr || City->bFallen)
		{
			continue;
		}

		// A SIEGE IS THE ONE THING IN THE GAME THAT STILL TAKES A SHARE OF A
		// CITY'S MAXIMUM, AND IT IS DELIBERATE. Issue #1331 turned every other
		// city damage number into points, because a share of the maximum divided
		// out of how long a city survived and made every city-health upgrade
		// worthless. The project owner was asked whether the Siege should follow
		// and answered on 2026-09-05, verbatim: "Keep it as a deliberate
		// exception (Recommended)" -- a siege does not care how thick your walls
		// are. So a fully invested city is no better off against a Siege than an
		// untouched one, which was stated and accepted, and a reader who finds a
		// percentage here has not found an oversight. `docs/DECISIONS.md` and
		// `UCataclysmEmpireMap::Bite` carry the same warning.
		//
		// THE SHARE IS TURNED INTO POINTS HERE, WHICH IS THE OTHER WAY ROUND
		// FROM BEFORE #1331. The growth used to be divided by the city's maximum
		// so that it could go through `Bite`; now that points are the currency
		// the map speaks, the flat share is multiplied instead and the division
		// -- and the guard against a maximum of zero it needed -- is gone.
		//
		// TWO AND A HALF POINTS OFF AN OUTPOST'S THOUSAND DEFENCE IS A FURTHER
		// QUARTER OF A PER CENT; off the Pillar's twenty thousand it is a
		// twentieth of that. See `SiegeDamageGrowthPerDay` for why that
		// asymmetry is the point rather than a rounding of it, and for the
		// ruling of 2026-09-06 that took the growth from 10 to 2.5.
		const float Grown = SiegeDamageGrowthPerDay * Host.Value;

		const float Defence = City->MaxDefence * SiegeDefenceBitePerDay + Grown;

		const float Population =
			City->MaxPopulation * SiegePopulationBitePerDay + Grown;

		OutReport.Besieged.Add(CityId);

		if (Map->Damage(CityId, Defence, Population))
		{
			CityFell(CityId, OutReport);
		}
	}
}

void UCataclysmEmpireRun::CityFell(int32 CityId, FCataclysmDayReport& OutReport)
{
	OutReport.Fallen.Add(CityId);

	// EVERY DUNGEON STANDING ON IT IS ABSORBED INTO THE ONE THE CITY BECOMES,
	// and how many there were sets both its depth and how many bosses it holds.
	const TArray<int32> Standing = DungeonsOn(CityId);

	for (const int32 DungeonId : Standing)
	{
		OutReport.Absorbed.Add(DungeonId);

		// `RemoveDungeon` AND NOT `ClearDungeon`. A dungeon absorbed by a city
		// falling was not beaten by anybody, so it must not pay out what
		// clearing one pays out. With the `RestoreDefenceOnClear` upgrade
		// bought, using `ClearDungeon` here would have the city's own killers
		// heal it on the way down.
		RemoveDungeon(DungeonId);
	}

	// AND THE CITY ITSELF BECOMES A DUNGEON. `docs/Cataclysm_GDD_v2.md` section
	// VIII: a fallen city "becomes a Dungeon City -- a staging ground with more
	// floors and multiple boss fights", and it "must be retaken to restore it".
	// `ClearDungeon` is where retaking happens.
	AddFallenCityDungeon(CityId, Standing.Num(), OutReport);

	// AND THE FALL IS ITSELF A SURGE. The design document says a city falling
	// triggers one; `UCataclysmSurgeScheduler::bCityFallAdvancesEscalation` is
	// what makes it also speed the rest of the run up.
	if (UCataclysmSurgeScheduler::bSurgeOnCityFall)
	{
		FireSurge(OutReport.Day, /* bFromCityFall */ true, OutReport);
	}
}

void UCataclysmEmpireRun::AddFallenCityDungeon(
	int32 CityId, int32 DungeonsAbsorbed, FCataclysmDayReport& OutReport)
{
	const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr;
	if (City == nullptr)
	{
		return;
	}

	// AN ERASED CITY LEAVES NOTHING TO RETAKE. The Void erases rather than
	// takes, and `UCataclysmEmpireMap::Retake` refuses an erased city, so a
	// dungeon standing on one could be cleared for ever without restoring it.
	// `Simulation._fall` skips it for the same reason.
	if (City->bErased)
	{
		return;
	}

	const FCataclysmDungeon Dungeon =
		UCataclysmSurgeScheduler::MakeFallenCityDungeon(
			NextDungeonId, *City, OutReport.Day, DungeonsAbsorbed);

	// THE SAME TWO STEPS A LANDED DUNGEON TAKES, in the same order. The
	// dungeons and the clock's timers are parallel lists kept in step by this
	// object and nothing else, so adding to one without the other is the
	// corruption `DungeonsAgreeWithTimers` exists to catch.
	if (Clock == nullptr || !Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors))
	{
		return;
	}

	Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays);

	Dungeons.Add(Dungeon);
	OutReport.Spawned.Add(Dungeon.DungeonId);

	NextDungeonId = FMath::Max(NextDungeonId, Dungeon.DungeonId + 1);
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
	// WHICH CITY IT WAS ON, WHAT KIND IT WAS, AND WHICH CATACLYSM SENT IT, READ
	// BEFORE IT IS REMOVED. `RemoveDungeon` takes the dungeon out of `Dungeons`,
	// so afterwards there is nothing left to ask.
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	const int32 CityId = Dungeon ? Dungeon->CityId : INDEX_NONE;
	const ECataclysmDungeonType Kind = Dungeon != nullptr
		? Dungeon->Type : ECataclysmDungeonType::Basic;
	const ECataclysmType Sender = Dungeon != nullptr
		? Dungeon->Cataclysm : ECataclysmType::None;

	if (!RemoveDungeon(DungeonId))
	{
		// NOTHING WAS CLEARED, SO NOTHING IS COUNTED. `Kind` above is a
		// placeholder for a dungeon that was not there, and this is the branch
		// that discards it: everything below runs only for a dungeon that
		// really did come off the map.
		return false;
	}

	const bool bWasFallenCity = Kind == ECataclysmDungeonType::FallenCity;

	// THE RUN REMEMBERS WHAT THE PLAYER BEAT. Issue #1324 slice 5.
	//
	// HERE AND NOT IN `RemoveDungeon`, which is the whole point of the two being
	// separate functions. A city that falls absorbs the dungeons standing on it
	// through `RemoveDungeon`, and nobody walked those -- counting them would
	// pay a player for losing a city and, for a Quest dungeon, would hand them
	// an objective for watching one be destroyed.
	++DungeonsCleared;

	// ORDINARY DUNGEONS ONLY, WHICH IS A DIFFERENT NUMBER AND NOT A SUBSET
	// KEPT FOR CONVENIENCE. `docs/Cataclysm_GDD_v2.md` section VIII: "Every
	// **ordinary** dungeon defeated adds one floor to the Cataclysm boss
	// dungeon. Quest dungeons and retaken Dungeon Cities do not". So the boss's
	// depth reads this and never `DungeonsCleared`, or pursuing the win
	// condition would make the final fight harder. Issue #1315 is the growth.
	if (Kind == ECataclysmDungeonType::Basic)
	{
		++BasicDungeonsCleared;
	}

	// ONE CLEARED QUEST DUNGEON IS ONE OBJECTIVE. The project owner ruled it on
	// 2026-09-06, verbatim "Yes -- one dungeon, one objective", and
	// `docs/Cataclysm_GDD_v2.md` section XI states it.
	//
	// AND IT COUNTS FOR THE CATACLYSM THAT SENT IT, which is what the unlock
	// rule reads. Issue #1357 put `FCataclysmDungeon::Cataclysm` on the dungeon
	// for this; before it, the only number that could be kept was the run's
	// total, and a total cannot say whether any one Cataclysm is finished.
	//
	// THE TOTAL IS STILL RAISED, and for a dungeon carrying no Cataclysm it is
	// the only thing raised. That is not dead code: a dungeon built by hand in a
	// test carries `None`, and so does a Fallen City -- which cannot be a Quest
	// dungeon, but the rule here is about what the field says rather than about
	// which kinds happen to reach it today.
	if (Kind == ECataclysmDungeonType::Quest)
	{
		++QuestObjectives;

		if (Sender != ECataclysmType::None)
		{
			int32& Earned = QuestObjectivesByCataclysm.FindOrAdd(Sender);
			++Earned;
		}
	}

	// BEATING A FALLEN CITY IS HOW A CITY COMES BACK. `docs/Cataclysm_GDD_v2.md`
	// section VIII: a Dungeon City "must be retaken to restore it".
	// `UCataclysmEmpireMap::Retake` is what half-restores it, and until this it
	// had no caller at all -- retaking was implemented and unreachable.
	//
	// BEFORE THE REPAIR UPGRADE BELOW AND NOT AFTER. `Retake` sets the city's
	// defence to half its maximum outright, so an upgrade applied first would be
	// overwritten; applied second it adds to the half, which is what a city that
	// bought it should get.
	if (bWasFallenCity && Map != nullptr && CityId != INDEX_NONE)
	{
		Map->Retake(CityId);
	}

	// AND THE CITY IS REPAIRED A LITTLE, IF IT BOUGHT THAT UPGRADE. The sheet
	// says "When you clear a dungeon, the city's Defense restores by an
	// ADDITIONAL 5%", and there is no base restore for it to be additional to:
	// neither this class nor `Simulation._clear` in the Python model gives a
	// city anything back for a dungeon being beaten. So the upgrade grants the
	// whole 5% rather than an increment on something. Issue #1267 asks whether
	// the base restore the word implies was meant to exist.
	if (Map != nullptr && CityId != INDEX_NONE)
	{
		if (FCataclysmCity* City = Map->FindMutable(CityId))
		{
			const float Restored = City->UpgradeValueFor(
				ECataclysmCityUpgradeEffect::RestoreDefenceOnClear);

			// A FALLEN CITY IS NOT REPAIRED BY THIS. It has no defence to
			// restore until it is retaken, and letting it climb off zero while
			// still marked fallen would put the map in a state nothing else
			// expects.
			if (Restored > 0.0f && !City->bFallen)
			{
				City->Defence = FMath::Min(
					City->MaxDefence,
					City->Defence + City->MaxDefence * Restored);
			}
		}
	}

	return true;
}

bool UCataclysmEmpireRun::RemoveDungeon(int32 DungeonId)
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
// City upgrades
// ---------------------------------------------------------------------------

ECataclysmCityUpgradeResult UCataclysmEmpireRun::WouldBuyCityUpgrade(
	int32 CityId, const FCataclysmCityUpgrade& Upgrade) const
{
	if (Map == nullptr || Clock == nullptr)
	{
		return ECataclysmCityUpgradeResult::RunHasNotBegun;
	}

	if (!Upgrade.IsValid())
	{
		return ECataclysmCityUpgradeResult::NotAnUpgrade;
	}

	// THE GUARD ON THE TWO CONSTANTS. An upgrade is free and immediate because
	// no cost and no build time is written anywhere and there is no gold. If
	// somebody raises either without building what would then be needed, every
	// purchase refuses instead of quietly handing out a paid upgrade for
	// nothing. Issue #1264.
	if (!UCataclysmCityUpgradeRules::IsFreeAndInstant())
	{
		return ECataclysmCityUpgradeResult::CannotPayYet;
	}

	if (!UCataclysmCityUpgradeRules::IsBuilt(Upgrade.Effect))
	{
		// REFUSED RATHER THAN SOLD. Fourteen of the 24 upgrades are waiting on a
		// system that does not exist, and a slot spent on one of those would buy
		// the player nothing at all.
		return ECataclysmCityUpgradeResult::EffectNotBuiltYet;
	}

	const FCataclysmCity* City = Map->Find(CityId);
	if (City == nullptr)
	{
		return ECataclysmCityUpgradeResult::NoSuchCity;
	}

	if (City->bFallen)
	{
		return ECataclysmCityUpgradeResult::CityHasFallen;
	}

	// A BESIEGED CITY CANNOT BE IMPROVED. The design document says a Siege
	// "Pauses city upgrades", and the project owner settled on 2026-09-05 that
	// this means it stops any upgrade part way through being built and blocks
	// buying new ones, while leaving the effects of upgrades the city already
	// holds working.
	//
	// ONLY THE BLOCKING HALF IS HERE, AND THE OTHER HALF IS NOT MISSING SO MUCH
	// AS IMPOSSIBLE. Nothing is ever part way through being built: `BuildDays`
	// is zero and every purchase is instant, so there is no in-progress build to
	// interrupt. Whoever gives an upgrade a build time under issue #1264 should
	// stop it here as well. Raising `BuildDays` alone would trip
	// `IsFreeAndInstant` above and refuse every purchase in the game.
	//
	// CHECKED BEFORE THE SLOT COUNT, so a player asking about a besieged city
	// that is also full is told the thing they can actually act on.
	if (IsBesieged(CityId))
	{
		return ECataclysmCityUpgradeResult::CityIsBesieged;
	}

	if (City->HasUpgrade(Upgrade.RowName))
	{
		return ECataclysmCityUpgradeResult::AlreadyBought;
	}

	if (Map->FreeUpgradeSlots(CityId) <= 0)
	{
		return ECataclysmCityUpgradeResult::NoSlotsLeft;
	}

	return ECataclysmCityUpgradeResult::Bought;
}

ECataclysmCityUpgradeResult UCataclysmEmpireRun::BuyCityUpgrade(
	int32 CityId, const FCataclysmCityUpgrade& Upgrade)
{
	// EVERY CHECK IS ABOVE, IN ONE PLACE. The city screen greys out what a city
	// cannot buy and says why, and it asks exactly this question rather than
	// deciding for itself, so the screen and the purchase cannot drift apart.
	const ECataclysmCityUpgradeResult Refusal =
		WouldBuyCityUpgrade(CityId, Upgrade);

	if (Refusal != ECataclysmCityUpgradeResult::Bought)
	{
		return Refusal;
	}

	FCataclysmCityUpgrade Bought = Upgrade;
	Bought.Tier = 1;

	// THE FIRST TRIGGER IS A FULL INTERVAL AWAY. An upgrade bought on day 37
	// with a 20 day interval first fires on day 57, rather than on whichever
	// coming day happens to divide by 20. Zero for every effect that is not one
	// of the two interval ones, and unread for them.
	if (Bought.IntervalDays > 0.0f)
	{
		Bought.NextTriggerDay =
			Clock->Day + FMath::Max(1, FMath::RoundToInt(Bought.IntervalDays));
	}

	// THE MAP RECORDS IT AND APPLIES WHAT BELONGS TO THE CITY: the two raised
	// maxima and the two restores. Everything below here is what the map cannot
	// do, because it involves dungeons.
	Map->AddUpgrade(CityId, Bought);

	if (Bought.Effect == ECataclysmCityUpgradeEffect::RemoveDungeons)
	{
		const TArray<int32> Standing = DungeonsOn(CityId);

		// SOONEST TO RESOLVE FIRST, so a one-time upgrade a player spent a slot
		// on clears the threat that was about to land rather than an arbitrary
		// one. The sheet says only "Remove 25% of dungeons on this city" and
		// names no order, so this is a judgement; `docs/DECISIONS.md` records
		// it.
		TArray<int32> ByUrgency = Standing;
		ByUrgency.Sort(
			[this](const int32 Left, const int32 Right)
			{
				return Clock->DaysUntilResolveFor(Left)
					   < Clock->DaysUntilResolveFor(Right);
			});

		// ROUNDED RATHER THAN FLOORED. A quarter of two dungeons removes one; a
		// quarter of one removes none, which is the honest answer for a
		// proportional effect on a single dungeon.
		const int32 ToRemove = FMath::Clamp(
			FMath::RoundToInt(ByUrgency.Num() * Bought.Value),
			0, ByUrgency.Num());

		for (int32 Index = 0; Index < ToRemove; ++Index)
		{
			// `RemoveDungeon` AND NOT `ClearDungeon`. Nobody walked these; they
			// were dispersed by building work, so they must not pay out what
			// beating a dungeon pays out.
			RemoveDungeon(ByUrgency[Index]);
		}
	}

	return ECataclysmCityUpgradeResult::Bought;
}

void UCataclysmEmpireRun::RunCityUpgradeIntervals(int32 Today)
{
	if (Map == nullptr)
	{
		return;
	}

	for (FCataclysmCity& City : Map->Cities)
	{
		// A FALLEN CITY REPAIRS NOTHING. It has no defence and no population to
		// recover until it is retaken, and letting either climb while the city
		// is still marked fallen would put the map in a state nothing else
		// expects.
		if (City.bFallen)
		{
			continue;
		}

		for (FCataclysmCityUpgrade& Upgrade : City.Upgrades)
		{
			const bool bIsInterval =
				Upgrade.Effect == ECataclysmCityUpgradeEffect::HealDefenceEvery
				|| Upgrade.Effect
					   == ECataclysmCityUpgradeEffect::RecoverPopulationEvery;

			if (!bIsInterval || Upgrade.IntervalDays <= 0.0f)
			{
				continue;
			}

			if (Today < Upgrade.NextTriggerDay)
			{
				continue;
			}

			if (Upgrade.Effect == ECataclysmCityUpgradeEffect::HealDefenceEvery)
			{
				City.Defence = FMath::Min(
					City.MaxDefence,
					City.Defence + City.MaxDefence * Upgrade.Value);
			}
			else
			{
				City.Population = FMath::Min(
					City.MaxPopulation,
					City.Population + City.MaxPopulation * Upgrade.Value);
			}

			// THE NEXT ONE IS AN INTERVAL FROM TODAY, not from the day it was
			// due. They are the same while days pass one at a time, and they
			// differ if a caller ever skips days; counting from today cannot
			// leave a trigger permanently in the past.
			Upgrade.NextTriggerDay =
				Today + FMath::Max(1, FMath::RoundToInt(Upgrade.IntervalDays));
		}
	}
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

	// WHAT THE PLAYER HAS DONE, WHICH NOTHING ELSE SAID. Every line above this
	// one describes what is being done TO the empire; a person reading a run had
	// no way to see what they had achieved in it. Issue #1324 slice 5.
	//
	// THE QUEST OBJECTIVES ARE NAMED SEPARATELY BECAUSE THEY ARE THE ONLY ONE OF
	// THE THREE THAT LEADS ANYWHERE. HOW MANY ARE NEEDED IS NOT SAID, and that
	// is honest rather than lazy: the requirement is per Cataclysm and this
	// module does not know which one is running. Printing "3 of 8" would be
	// inventing a denominator. Issue #1357.
	Lines.Add(FString::Printf(
		TEXT("%d dungeons cleared, %d of them ordinary. %d quest objectives "
			 "earned. %d resolves have cost a city."),
		DungeonsCleared, BasicDungeonsCleared, QuestObjectives,
		DungeonsDetonated));

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

			// THE KIND AND THE SUB-TYPE ARE SAID OUT LOUD BECAUSE NOTHING ELSE
			// SAYS THEM. A Siege takes a share of its host every day and a Cow
			// Level costs twice the days to walk, and until this line a person
			// reading the run could not tell which dungeon was which.
			//
			// THE KIND JOINED IT FOR THE OBJECTIVE COUNT. A Quest dungeon is
			// the only standing dungeon that earns an objective, and the line
			// above says how many have been earned; without this a reader could
			// see the total and not see which dungeon on the board would add to
			// it. Issue #1324 slice 5.
			//
			// AN ORDINARY DUNGEON WITH NO SUB-TYPE ADDS NOTHING HERE, which is
			// most of them: `KindName` answers an empty string for `Basic` and
			// `SubTypeName` one for `None`, for the same reason.
			TArray<FString> Marks;

			const FString Kind =
				UCataclysmSurgeScheduler::KindName(Dungeon.Type);
			if (!Kind.IsEmpty())
			{
				Marks.Add(Kind);
			}

			const FString SubType =
				UCataclysmSurgeScheduler::SubTypeName(Dungeon.SubType);
			if (!SubType.IsEmpty())
			{
				Marks.Add(SubType);
			}

			const FString Marked = Marks.IsEmpty()
				? FString()
				: FString::Printf(TEXT(" (%s)"),
								  *FString::Join(Marks, TEXT(", ")));

			Lines.Add(FString::Printf(
				TEXT("  dungeon %d%s on %s: %d floors, %.1f days until it "
					 "resolves"),
				Dungeon.DungeonId,
				*Marked,
				City ? *City->Name : TEXT("nowhere"),
				Dungeon.Floors,
				Clock->DaysUntilResolveFor(Dungeon.DungeonId)));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}
