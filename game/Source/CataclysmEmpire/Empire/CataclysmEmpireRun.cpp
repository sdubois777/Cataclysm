// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSeedMix.h"

namespace
{
	/** A city's figures just before a hit is dealt to it, so `NoteHitIn` can say what the hit took. Issue #2733 slice 3. */
	struct FCityBeforeAHit
	{
		int32 CityId = INDEX_NONE;
		float Defence = 0.0f;
		float Population = 0.0f;
		bool bStanding = false;
	};

	FCityBeforeAHit BeforeAHit(const UCataclysmEmpireMap* Map, int32 CityId)
	{
		FCityBeforeAHit Before;
		Before.CityId = CityId;
		if (const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr)
		{
			// AS THE CITY IS READ, so a hit after a respec lowered a maximum records what the hit took and not the clamp
			// down to that maximum. Issue #2741 slice 3a.
			Before.Defence = Map->DefenceOf(*City);
			Before.Population = Map->PopulationOf(*City);
			Before.bStanding = !City->bFallen;
		}
		return Before;
	}

	/**
	 * One entry in the day's `Hits`: the city's figures before the hit less its figures now. RECORDS AND CHANGES
	 * NOTHING: the ruling of 2026-11-17 made this additive only, and nothing the day loop computes reads it.
	 */
	void NoteHitIn(FCataclysmDayReport& OutReport, const UCataclysmEmpireMap* Map, const FCityBeforeAHit& Before,
				   ECataclysmCityHitKind Kind)
	{
		const FCataclysmCity* After = Map != nullptr ? Map->Find(Before.CityId) : nullptr;
		if (After == nullptr)
		{
			return;
		}
		FCataclysmCityHit& Hit = OutReport.Hits.AddDefaulted_GetRef();
		Hit.CityId = Before.CityId;
		Hit.DefenceLost = Before.Defence - Map->DefenceOf(*After);
		Hit.PopulationLost = Before.Population - Map->PopulationOf(*After);
		Hit.Kind = Kind;
		Hit.bFelledTheCity = Before.bStanding && After->bFallen;
	}

	/**
	 * Reads the city when made and records the hit (`NoteHitIn`) when its scope ends, by any path. FOR THE QUEST TIMER'S
	 * HIT, whose fall returns from inside the braces that hold only `CityFell(HostId, OutReport); return;`: a check in
	 * `tools/tests/test_the_hollow_in_the_model_and_in_the_game.py` reads that text exactly, and a record written
	 * inside them would change it. `CityFell` changes neither the city's defence nor its people, so recording after it
	 * records what the hit took.
	 */
	struct FHitRecordedOnLeaving
	{
		FHitRecordedOnLeaving(FCataclysmDayReport& InReport, const UCataclysmEmpireMap* InMap, int32 CityId,
							  ECataclysmCityHitKind InKind)
			: Report(InReport), Map(InMap), Before(BeforeAHit(InMap, CityId)), Kind(InKind)
		{
		}
		~FHitRecordedOnLeaving() { NoteHitIn(Report, Map, Before, Kind); }
		FHitRecordedOnLeaving(const FHitRecordedOnLeaving&) = delete;
		FHitRecordedOnLeaving& operator=(const FHitRecordedOnLeaving&) = delete;

		FCataclysmDayReport& Report;
		const UCataclysmEmpireMap* Map;
		FCityBeforeAHit Before;
		ECataclysmCityHitKind Kind;
	};
}

// ---------------------------------------------------------------------------
// Starting a run
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::Begin(int32 InSeed, ECataclysmSurgeMode Mode,
								int32 LethalityRung, int32 InDifficultyTier)
{
	// NO CHARACTER NAMED, SO THE RUN'S OWN SEED DRAWS ITS CATACLYSMS, as every run
	// did before issue #1324 slice 6a. See `BeginForCharacter`.
	BeginForCharacter(InSeed, InSeed, Mode, LethalityRung, InDifficultyTier);
}

void UCataclysmEmpireRun::BeginForCharacter(int32 InSeed, int32 InCataclysmOrderSeed,
											ECataclysmSurgeMode Mode, int32 LethalityRung,
											int32 InDifficultyTier)
{
	Map = NewObject<UCataclysmEmpireMap>(this);

	// THE TREE BEFORE THE CITIES, so they are built full at the maxima it raises (S1). See `TreeBonuses`.
	Map->TreeBonuses = TreeBonuses;
	Map->Build();

	// HERETIC GIVES A CITY TWO UPGRADE SLOTS INSTEAD OF THREE. The map cannot
	// work that out for itself: the lethality rung belongs to the run, and a map
	// built on its own keeps the ordinary three.
	Map->UpgradeSlots = UCataclysmCityUpgradeRules::SlotsFor(LethalityRung);

	Clock = NewObject<UCataclysmDayClock>(this);

	// THE TREE'S DAYS ON A TIMER ARE ASKED OF THIS RUN, which knows the dungeon's city. Weak, so a clock that outlives
	// its run asks nothing. Issue #2741 slice 3a.
	Clock->ResolveBonusDays = [WeakRun = TWeakObjectPtr<UCataclysmEmpireRun>(this)](int32 DungeonId)
	{
		const UCataclysmEmpireRun* Run = WeakRun.Get();
		return Run != nullptr ? Run->ResolveBonusDaysFor(DungeonId) : 0.0f;
	};

	// AND TODAY, ASKED OF THIS RUN, which Enduring Foundations reads on the map. Weak for the clock's reason. Issue #2741
	// slice 3b.
	Map->TodayIs = [WeakRun = TWeakObjectPtr<UCataclysmEmpireRun>(this)]()
	{
		const UCataclysmEmpireRun* Run = WeakRun.Get();
		return Run != nullptr ? Run->Day() : 0;
	};

	// AND ECONOMIC BAILOUT, ASKED OF THIS RUN when a city's defence reaches 0. Weak for the clock's reason. Issue #2741
	// slice 9.
	Map->SaveFromFalling = [WeakRun = TWeakObjectPtr<UCataclysmEmpireRun>(this)](int32 CityId)
	{
		UCataclysmEmpireRun* Run = WeakRun.Get();
		return Run != nullptr && Run->SaveCityFromFalling(CityId);
	};

	Surges = NewObject<UCataclysmSurgeScheduler>(this);
	Surges->Mode = Mode;
	Surges->LethalityRung = LethalityRung;
	Surges->GapBonusDays = TreeBonuses.SurgeGapDays;

	Dungeons.Reset();
	NextDungeonId = 0;

	// KEPT, RATHER THAN ONLY USED. `GiveModifiers` asks how many modifiers a
	// dungeon carries long after `Begin` has returned, and the answer is one per
	// difficulty tier. It was a parameter and nothing else until then.
	DifficultyTier = InDifficultyTier;

	// A SECOND `Begin` IS A FRESH RUN, so what the last one achieved goes with
	// it. Leaving these standing would carry one campaign's quest objectives
	// into the next, and the win condition slice 6 builds reads them. Issue
	// #1324 slice 5.
	DungeonsCleared = 0;
	BasicDungeonsCleared = 0;
	QuestObjectives = 0;
	DungeonsDetonated = 0;
	QuestHits = 0;
	LastHitDay = 0;
	LastHitCityId = INDEX_NONE;
	bLastHitWasAQuestTimer = false;
	LastMoveDay = 0;
	LastMoveFromCityId = INDEX_NONE;
	LastMoveToCityId = INDEX_NONE;
	LastDeathDay = 0;
	LastDeathDungeonName.Reset();
	LastDeathWalkDays = 0;
	LastDeathDaysCost = 0;
	LastDeathFallenCityIds.Reset();
	// AND NO CLEAR YET. Issue #2733 slice 3.
	LastClearDay = INDEX_NONE;
	LastClearDungeonName.Reset();
	LastClearWalkDays = 0;
	LastDaysAway = FCataclysmDaysAway();
	SurgesFromEmptyBoard = 0;
	OwedDays = 0;

	// AND THE EMPIRE UPGRADE TREE'S EVENT COUNTS START AGAIN: no clear since a surge, no city reclaimed, and the Last
	// Stand not yet spent, which is how it "Resets every tier" (Q3). Issue #2741 slice 3b.
	ClearsSinceLastSurge = 0;
	CitiesReclaimed = 0;
	bSanctuaryLastStandSpent = false;

	// AND RELENTLESS PURSUIT'S CLEARS IN A ROW: a new run has cleared nothing yet. Issue #2741 slice 5b (D5).
	ClearsSinceLastDeath = 0;

	// AND STIMULUS PACKAGE'S FLAG: a new run's population has not fallen. Issue #2741 slice 6b (B2).
	bPopulationFellBelowHalf = false;

	// AND THE CLEANSE MAY BE BOUGHT AGAIN: once a run (issue #2838).
	bCleanseBoughtThisRun = false;

	// AND NO DEFEAT HAS AWARDED ANYTHING IN THIS RUN YET. Issue #52.
	LastEmpirePointsAwarded = 0.0;

	// AND THE CATACLYSM BOSS DUNGEON IS SHUT AGAIN. A second `Begin` carrying the
	// last run's opened boss would stop this run ever opening its own. Issue
	// #1324 slice 6b.
	bCataclysmDungeonOpened = false;
	CataclysmDungeonOpenedDay = 0;
	CataclysmDungeonBaseFloors = 0;

	// AND IT IS NOT WON, AND CARRIES NO WIN LINE: the caller that begins the run after a win notes the last one's
	// afterwards (`NoteLastRunWon`), so a fresh run begun any other way says nothing about one. Issue #1324 slice 6e.
	bWon = false;
	WonDay = 0;
	LastRunWonDay = 0;
	LastRunWonTier = 0;
	LastRunWonCataclysm = ECataclysmType::None;
	LastRunWonPassivePoints = 0;

	Stream.Initialize(InSeed);

	// WHICH CATACLYSMS THIS CHARACTER FACES. How many comes from the difficulty
	// tier and which ones from the CHARACTER'S order seed, not the run's, so a
	// character's next run at the same tier meets the same Cataclysms and one a
	// tier higher meets those plus one -- the project owner's ruling of
	// 2026-09-06, and "The same tier means the same Cataclysms, by name and not
	// only by count" (`docs/Cataclysm_GDD_v2.md`, Ending a Run). Issues #1338,
	// #1357 and #1324 slice 6a.
	ActiveCataclysms = UCataclysmRoster::ActiveFor(InCataclysmOrderSeed, InDifficultyTier);

	// AND THE FLOORS THE EMPIRE UPGRADE TREE ADDS TO EVERY DUNGEON MADE, which need the active types for Infinite
	// Depths, so they are set here once those are known and before the opening surge makes a dungeon. Issue #2741 slice
	// 4, W3.
	Surges->FloorsTheTreeAdds = TreeBonuses.FloorsAddedTo(/*bTheBoss*/ false, ActiveCataclysms.Num());

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
	// THROUGH THE MIX since issue #2749's placement seeding slice, ruled by the coordinating session on 2026-11-19. Until
	// it, this and `ModifierStream` were `MixedSeed(InSeed, WaveSalt)` and `MixedSeed(InSeed, ModifierSalt)`; `MixedSeed`
	// is linear, so the two seeds were a fixed distance apart in every run and their draws at one index related. A RUN
	// LOADED FROM A SAVE KEEPS ITS DRAWS: the save stores each stream's current state, not this seed.
	CataclysmStream.Initialize(CataclysmSeedMix::Mix(InSeed, UCataclysmRoster::WaveSalt, 0));

	// AND A THIRD, FOR THE MODIFIER DRAW. See `ModifierStream`: taking those
	// draws from either of the streams above would shift every later draw made
	// on it, and re-roll every fixed-seed test in the project.
	ModifierStream.Initialize(CataclysmSeedMix::Mix(InSeed, UCataclysmRoster::ModifierSalt, 0));

	// AND THE CORRUPTED STALKER'S SEED, which is not a stream: each dungeon makes its own from it. See
	// `UCataclysmDungeonModifierRules::CarriesTheCorruptedStalker`.
	CorruptedStalkerSeed = UCataclysmRoster::MixedSeed(
		InSeed, UCataclysmDungeonModifierRules::CorruptedStalkerSalt);

	// AND THE MASK ON EVERY DUNGEON'S WALK SEED, so this run's dungeons carve floors no other run's do. Issue #2749
	// (B10). NEVER BELOW 0, so a dungeon's seed is never below 0 either. Set here, before the opening surge makes a
	// dungeon, because `GiveModifiers` rolls the Corrupted Stalker on the walk seed as each dungeon is made.
	WalkSeedMask = UCataclysmRoster::MixedSeed(InSeed, WalkSeedSalt) & 0x7FFFFFFF;

	// THE POOL IS NOT CLEARED BY A FRESH RUN. It is the modifier TABLE and not
	// anything this campaign did, and the caller that filled it in did so once.
	// Clearing it here would make `Begin` a second time silently stop giving
	// dungeons modifiers.
}

FCataclysmDayReport UCataclysmEmpireRun::FireOpeningSurge()
{
	FCataclysmDayReport Report;

	if (Map == nullptr || Clock == nullptr || Surges == nullptr)
	{
		// `Begin` HAS NOT RUN, as in `AdvanceDay`: an empty report, not a read
		// through a null map.
		return Report;
	}

	// DAY 0, NOTHING FIRED YET, AND ONE DUE: the three refusals that make a
	// second call, a restored run and a run already past its first day all do
	// nothing. See the header. Issue #2733.
	const int32 Today = Clock->Day;
	Report.Day = Today;
	if (Today != 0 || Surges->SurgesFired != 0 || !Surges->IsDue(Today))
	{
		return Report;
	}

	// THE SAME CALL `AdvanceDay` MAKES FOR THE CLOCK'S SURGE, with today's
	// number, so the wave, its modifiers, its timers and the schedule's move
	// are all made the one way. The clock is not moved: the day has not passed.
	FireSurge(Today, /* bFromCityFall */ false, Report);

	return Report;
}

void UCataclysmEmpireRun::GiveModifiers(FCataclysmDungeon& Dungeon)
{
	GiveModifiersFrom(Dungeon, ModifierStream);
}

void UCataclysmEmpireRun::GiveModifiersFrom(FCataclysmDungeon& Dungeon, FRandomStream& Draws)
{
	if (ModifierPool.Num() == 0)
	{
		// NOTHING, AND NOT AN EMPTY LIST. See the header: a dungeon a test built
		// by hand keeps the modifiers the test gave it.
		return;
	}

	const TArray<FCataclysmDungeonModifier> Pool =
		UCataclysmDungeonModifierRules::PoolFor(ModifierPool, ActiveCataclysms);

	const int32 Count = UCataclysmDungeonModifierRules::CountFor(
		DifficultyTier, Dungeon.SubType);

	const TArray<FCataclysmDungeonModifier> Drawn =
		UCataclysmDungeonModifierRules::Draw(Pool, Count, Draws); // `ModifierStream` but for a bonus dungeon (slice 9).

	Dungeon.Modifiers = UCataclysmDungeonModifierRules::KeysOf(Drawn);
	Dungeon.ModifierScore = UCataclysmDungeonModifierRules::DangerOf(Drawn);

	// THE CORRUPTED STALKER, OUTSIDE THE SLOTS AND OFF `ModifierStream`. Issue #1308: one dungeon in twenty, and it
	// still adds its weight to the Modifier Score. Its weight is the table's row, so a pool without the row grants
	// nothing; the row is never in `Pool`, because `PoolFor` leaves the `Generic` column out.
	const FName StalkerKey = UCataclysmDungeonModifierRules::CorruptedStalkerKey();
	const FCataclysmDungeonModifier* Stalker = ModifierPool.FindByPredicate(
		[&StalkerKey](const FCataclysmDungeonModifier& Row) { return Row.RowKey == StalkerKey; });
	if (Stalker
		&& UCataclysmDungeonModifierRules::CarriesTheCorruptedStalker(CorruptedStalkerSeed,
																	   Dungeon.WalkSeed(WalkSeedMask)))
	{
		Dungeon.Modifiers.Add(Stalker->RowKey);
		Dungeon.ModifierScore += Stalker->Danger;
	}
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
// The Cataclysm boss dungeon, once earned -- issue #1324 slice 6b
// ---------------------------------------------------------------------------

int32 UCataclysmEmpireRun::CataclysmDungeonFloorsNow() const
{
	if (!bCataclysmDungeonOpened)
	{
		return 0;
	}

	// THE ROLL AND THE COUNT. Every ordinary dungeon defeated
	// this run is in `BasicDungeonsCleared`, those before the boss opened and
	// those after, so this is the design's rule with no cap.
	//
	// AND THE EMPIRE UPGRADE TREE'S FLAT FLOORS, NOT INFINITE DEPTHS, whose text excludes the boss: the coordinating
	// session's W3 of 2026-11-19, as the simulation's `_make_dungeon` gives the boss the flat floors. Counted here, where
	// the boss's floors are counted, never below one. Issue #2741 slice 4.
	return FMath::Max(1, CataclysmDungeonBaseFloors
		+ BasicDungeonsCleared
			* UCataclysmSurgeScheduler::CataclysmFloorsPerOrdinaryDungeon
		+ TreeBonuses.FloorsAddedTo(/*bTheBoss*/ true, ActiveCataclysms.Num()));
}

const FCataclysmDungeon* UCataclysmEmpireRun::FindCataclysmDungeon() const
{
	return Dungeons.FindByPredicate(
		[](const FCataclysmDungeon& Candidate)
		{
			return Candidate.Type == ECataclysmDungeonType::Cataclysm;
		});
}

void UCataclysmEmpireRun::OpenCataclysmDungeonIfEarned()
{
	// ONCE A RUN. The flag outlives the dungeon, so a quest clear after the boss
	// has left the board does not bring another.
	if (bCataclysmDungeonOpened || Clock == nullptr
		|| !IsCataclysmDungeonUnlocked())
	{
		return;
	}

	// THE NEWEST ACTIVE CATACLYSM SENDS IT: the last of the list, which is the
	// one the character's draw added last. Ruled by the coordinating session for
	// the slice; the model stamps `active_types[-1]`.
	const ECataclysmType Newest = ActiveCataclysms.Num() > 0
		? ActiveCataclysms.Last() : ECataclysmType::None;

	FCataclysmDungeon Dungeon = UCataclysmSurgeScheduler::MakeCataclysmDungeon(
		NextDungeonId, Day(), Stream, Newest);

	// IT CARRIES MODIFIERS, as every dungeon the player walks does: one per
	// difficulty tier, from the active Cataclysms' pool. The model's
	// `_make_dungeon` samples them for a Cataclysm dungeon too.
	GiveModifiers(Dungeon);

	// THE SAME TWO STEPS EVERY DUNGEON TAKES ONTO THE BOARD, so the dungeons and
	// the timers stay in step (`DungeonsAgreeWithTimers`). Its timer is
	// `UCataclysmSurgeScheduler::NeverResolveDays` and never runs out.
	if (!Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors))
	{
		return;
	}

	Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays);

	bCataclysmDungeonOpened = true;
	CataclysmDungeonOpenedDay = Dungeon.SpawnedDay;
	CataclysmDungeonBaseFloors = Dungeon.Floors;

	Dungeons.Add(Dungeon);
	NextDungeonId = FMath::Max(NextDungeonId, Dungeon.DungeonId + 1);

	// AND AS DEEP AS THE ORDINARY DUNGEONS ALREADY DEFEATED MAKE IT.
	DeepenCataclysmDungeon();
}

void UCataclysmEmpireRun::DeepenCataclysmDungeon()
{
	FCataclysmDungeon* Boss = Dungeons.FindByPredicate(
		[](const FCataclysmDungeon& Candidate)
		{
			return Candidate.Type == ECataclysmDungeonType::Cataclysm;
		});

	if (Boss == nullptr || !bCataclysmDungeonOpened)
	{
		return;
	}

	Boss->Floors = CataclysmDungeonFloorsNow();

	// ONE DAY A FLOOR, the starting rate: no city upgrade reaches a dungeon on no
	// city. A Cow Level's doubling is kept should one ever grow, which a
	// Cataclysm cannot be today -- the model's `_walk_days` keeps it the same way.
	Boss->WalkDays = FMath::Max(1.0f, Boss->Floors * UCataclysmDayClock::DaysPerFloor);
	if (Boss->SubType == ECataclysmDungeonSubType::CowLevel)
	{
		Boss->WalkDays *= UCataclysmSurgeScheduler::CowLevelWalkMultiplier;
	}

	// THE CLOCK'S COPY OF ITS FLOORS, NOT ITS TIMER, which never runs out. The
	// save writes the whole timer list, so a stale copy would reach the disk.
	if (Clock != nullptr)
	{
		Clock->SetFloors(Boss->DungeonId, Boss->Floors);
	}
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
	// A TEST PINS THIS. `Cataclysm.EmpireRun.TheReportedDayIsAlwaysTheClocksDay` fails
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
	// A CITY UPGRADE FINISHED TODAY IS FINISHED BEFORE THE REPAIRS AND THE ASSAULTS, for the reason above: a wall
	// that is done today stands against today's bites. A fallen or besieged city counts no day of its build. A Siege a
	// surge landed a moment ago pauses it from today. Issue #2825.
	AdvanceCityBuilds(Today, Report);

	RunCityUpgradeIntervals(Today);

	// AND THE EMPIRE UPGRADE TREE'S TWO REPAIRS, at the same point for the same reason. Issue #2741 slice 3b.
	RunTreeIntervals(Today);

	// EVERY TIMER MOVES, INCLUDING ONE THAT ARRIVED A MOMENT AGO. That is what
	// the model does: a dungeon spawned today has a day taken off it today.
	//
	// AND THE DAY TURNS, WHICH IS WHEN ENDURING FOUNDATIONS REACHES A CITY UPGRADE HELD TEN DAYS and raises the maximum
	// it gives: every city keeps its share of its maxima across the turn, by the owner's rule for the tree of 2026-11-23,
	// "It should maintain the % they were at before buying the upgrades". Issue #2859. Nothing else in the turn moves a
	// maximum, so on any other day nothing is written.
	const FCataclysmCityShares BeforeTheDayTurns = Map->SharesNow();
	const TArray<int32> Resolved = Clock->AdvanceDay();
	Map->KeepShares(BeforeTheDayTurns);

	Report.Resolved = Resolved;

	// A SIEGE TAKES ITS SHARE BEFORE ANY TIMER IS ACTED ON, so the day's two
	// kinds of assault land together and after the repairs above. A city felled
	// here loses its ordinary dungeons with it, and `ResolveDungeon` below
	// finds nothing to resolve for them, which is the right answer: a city
	// cannot be bitten twice for falling once. A Quest dungeon the fall moved
	// is still on the map, since 2026-10-21, and if its timer ran out today it
	// resolves below on the city it was moved to, as the model's does.
	ApplySiegeDamage(Report);

	// A HOLLOW LOSES ITS FLOOR FOR THE DAY, before its timer is acted on, which
	// is where `Simulation.step` calls `_hollow_shrink`. Since 2026-10-22.
	ShrinkHollows(Today);

	for (const int32 DungeonId : Resolved)
	{
		// A SCOUT PARTIES BONUS DUNGEON LEAVES INSTEAD, with no consequence (issue #2741 slice 9, V16).
		if (!LetABonusDungeonLeave(DungeonId))
		{
			ResolveDungeon(DungeonId, Report);
		}
	}

	// AND STIMULUS PACKAGE'S CHECK, AFTER THE DAY'S BITES (issue #2741 slice 6b, B2).
	NotePopulationAgainstHalf();

	// AND THE DAY'S INCOME, AFTER THEM TOO: Pax Imperialis and Wealth Interest pay through the game instance (issue #2741
	// slice 6c, B8).
	if (OnDayPassed)
	{
		OnDayPassed(*this, Day());
	}

	Report.bPillarExposed = Map->IsPillarExposed();

	// THE BOARD IS EMPTY, SO THE NEXT WAVE COMES NOW. The project owner's rule
	// of 2026-09-07, verbatim: "Anytime there are no longer dungeons on the
	// board, a surge happens." Issue #1414 ports it from
	// `Simulation._maybe_surge_on_empty_board`.
	//
	// LAST IN THE DAY, WHICH IS WHERE `Simulation.step` ASKS. Every removal the
	// day itself makes is above this line -- `CityFell` absorbs the ordinary
	// dungeons on a city that `ApplySiegeDamage` or `ResolveDungeon` felled, and
	// `ResolveDungeon` takes off an ordinary dungeon that hit its city -- and
	// a dungeon the player cleared since yesterday is already gone. Since
	// 2026-10-13 a wave of ordinary dungeons left alone empties the board by
	// itself, as each hits and leaves. So a clear made after
	// day N is seen on day N+1, which is the model's timing.
	//
	// AFTER THE TIMERS HAVE MOVED, so this wave does not lose a day on the day
	// it lands, where a wave the clock sends does. That difference is the
	// model's: its scheduled surge fires above its timer loop and this one
	// below it.
	//
	// NOT WHILE THE PILLAR CAN BE REACHED. The model opens the Last Stand
	// there, which is a dungeon standing on the board, so its board is never
	// empty from that day on and this never fires. The Last Stand is not built
	// here -- issue #43 -- so the condition it would have supplied is asked
	// directly.
	//
	// THROUGH `FireSurge` WITH NO CITY-FALL FLAG, as the clock's surge goes. It moves `NextSurgeDay` on from today and
	// advances the escalation counter: it is the scheduled wave arriving early, not an extra one.
	//
	// ONLY ORDINARY DUNGEONS COUNT, AND THE WAVE IS LARGER, since the owner's rulings on issue #2865: of 2026-11-23,
	// "The board counts as empty when no ordinary dungeon stands. Quest dungeons, the Cataclysm boss dungeon and Fallen
	// City dungeons do not count", with no minimum gap (`IsDueOnEmptyBoard`); and of 2026-11-24, chosen option
	// verbatim "Flat +4, no stacking" (`ExtraDungeonsOnEmptyBoard`). A Scout Parties bonus dungeon is still not
	// counted (slice 9, V16).
	if (!Report.bPillarExposed
		&& Surges->IsDueOnEmptyBoard(Today, OrdinaryDungeonCount()))
	{
		FireSurge(Today, /* bFromCityFall */ false, Report, /* bFromEmptyBoard */ true);

		Report.bSurgedOnEmptyBoard = true;
		++SurgesFromEmptyBoard;
	}

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
	//
	// INSIDE A DUNGEON THE WHOLE DAY IS OWED AND NOT RUN, the owner's decision of 2026-10-24: nothing resolves
	// until the player leaves, and `SettleDaysOwed` runs these then, in order.
	const bool bInsideADungeon = IsAWalkUnderWay();
	while (Clock->TakeAWholeDay(Days))
	{
		if (bInsideADungeon)
		{
			++OwedDays;
		}
		else
		{
			Reports.Add(AdvanceDay());
		}

		// THE COST IS ADDED ONCE. Asking again with zero drains whatever is left
		// in the carry -- a floor costing three days would otherwise leave two
		// of them unspent until the next floor.
		Days = 0.0f;
	}

	return Reports;
}

int32 UCataclysmEmpireRun::DaysOwed() const
{
	return OwedDays;
}

TArray<FCataclysmDayReport> UCataclysmEmpireRun::SettleDaysOwed()
{
	// OWED NONE BEFORE THE FIRST OF THEM RUNS, so nothing those days do can see a debt that is being paid.
	const int32 Owed = OwedDays;
	OwedDays = 0;

	return AdvanceDays(Owed);
}

bool UCataclysmEmpireRun::IsAWalkUnderWay() const
{
	return Clock != nullptr && Clock->CurrentDungeonId != INDEX_NONE;
}

TArray<FCataclysmTimerForecast> UCataclysmEmpireRun::TimersRunningOutWithin(int32 Days) const
{
	TArray<FCataclysmTimerForecast> Forecast;
	if (Days <= 0 || Clock == nullptr || Map == nullptr)
	{
		return Forecast;
	}

	// IN THE CLOCK'S ORDER, the order `UCataclysmDayClock::AdvanceDay` resolves one day's timers in; sorted by day below,
	// stably, so two on one day keep it.
	for (const FCataclysmDungeonTimer& Timer : Clock->Timers)
	{
		// THE DUNGEON STOOD IN DOES NOT MOVE (`UCataclysmDayClock::bTimerTicksWhileRunning`).
		if (Timer.DungeonId == Clock->CurrentDungeonId && !UCataclysmDayClock::bTimerTicksWhileRunning)
		{
			continue;
		}
		const FCataclysmDungeon* Dungeon = FindDungeon(Timer.DungeonId);
		if (Dungeon == nullptr || Dungeon->bBonus
			|| (Dungeon->Type != ECataclysmDungeonType::Basic && Dungeon->Type != ECataclysmDungeonType::Quest))
		{
			continue;
		}
		const FCataclysmCity* City = Map->Find(Dungeon->CityId);
		if (City == nullptr || City->bFallen)
		{
			continue;
		}

		// THE FIRST DAY THAT TAKES IT TO NOUGHT: a day comes off, then the clock asks, so a timer already at nought runs
		// out on the next day passed and never on none.
		const int32 DaysToRunOut = FMath::Max(1, FMath::CeilToInt(Clock->DaysUntilResolveFor(Timer.DungeonId)));
		if (DaysToRunOut > Days)
		{
			continue;
		}

		FCataclysmTimerForecast& Each = Forecast.AddDefaulted_GetRef();
		Each.DungeonId = Timer.DungeonId;
		Each.Day = Day() + DaysToRunOut;
		Each.CityId = Dungeon->CityId;
	}

	Forecast.StableSort([](const FCataclysmTimerForecast& A, const FCataclysmTimerForecast& B) { return A.Day < B.Day; });
	return Forecast;
}

// ---------------------------------------------------------------------------
// A surge
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::FireSurge(int32 Today, bool bFromCityFall,
									FCataclysmDayReport& OutReport, bool bFromEmptyBoard)
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
		// A SCOUT PARTIES BONUS DUNGEON IS NOT COUNTED against a city's cap: it threatens nothing (V16). Slice 9.
		if (Standing.bBonus)
		{
			continue;
		}
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

	// EACH DUNGEON IS STAMPED WITH THE CATACLYSM THAT SENT IT, AS IT IS MADE.
	// Issue #1357 for the stamp; `docs/DECISIONS.md`, 2026-10-21, for moving it.
	//
	// THE RUN DRAWS IT AND THE SCHEDULER ASKS FOR IT. The scheduler is
	// deliberately ignorant of which Cataclysms this campaign faces -- it must
	// stay able to roll a wave against a bare map in a test, the same reason the
	// dungeon and siege counts are passed in -- so the active set stays here and
	// `RollWave` is handed a way to draw from it. Until 2026-10-21 the run
	// stamped the wave after `RollWave` returned. It moved because a Quest
	// dungeon's sub-type now depends on its sender, so the sender has to be
	// known while the dungeon is made: `Simulation.trigger_surge` sets
	// `making_for` around `_make_dungeon` for the same reason.
	//
	// THE CATACLYSM STREAM TAKES THE SAME DRAWS IN THE SAME ORDER: one per
	// dungeon made, asked in the order they are made, which is the order the
	// loop that used to stand here walked the wave. `Stream` is not touched by
	// it. So a seed sends the same Cataclysms to the same dungeons it did.
	// Since 2026-10-22 `PickTargets` is what asks, once per city landed on and
	// before that slot is rolled, because the Void aims by its stacks; the
	// count and the order are the same.
	//
	// BEFORE THE CLOCK IS TOLD ANYTHING, so a dungeon cannot reach `Dungeons`
	// carrying `None` and be corrected afterwards. There is no correcting it:
	// `ClearDungeon` reads this field to decide which Cataclysm an objective
	// counts for, and a dungeon that reached the map unstamped would earn the
	// player nothing when they cleared it.
	TArray<FCataclysmDungeon> Wave =
		Surges->RollWave(*Map, Today, NextDungeonId, Stream, DungeonsPerCity,
						 SiegesStanding,
						 [this]() { return RollCataclysm(); }, bFromEmptyBoard);

	for (FCataclysmDungeon& Dungeon : Wave)
	{
		// AND ITS MODIFIERS, HERE AND NOT IN THE SCHEDULER. The scheduler knows
		// neither which Cataclysms this campaign faces nor what tier it is
		// played at, and the draw needs both. Issue #41.
		//
		// AFTER THE DUNGEON IS MADE, AND THE ORDER MATTERS NOW. How many it
		// draws comes from `CountFor`, which reads the sub-type, and since
		// 2026-10-21 a Famine Quest dungeon's sub-type is Sacrificial whatever
		// it rolled -- so it draws double. The pool is still the union of the
		// ACTIVE Cataclysms' modifiers and not the sender's.
		GiveModifiers(Dungeon);
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
		++OutReport.SurgeDungeons; // A surge's: one it landed, for the days-away summary. Additive only (#2733).

		NextDungeonId = FMath::Max(NextDungeonId, Dungeon.DungeonId + 1);
	}

	// THE SCHEDULE MOVES WHETHER OR NOT ANYTHING LANDED. A wave that found no
	// target still counts as the surge for that period; not moving it would
	// fire a surge every day for ever.
	//
	// THE MODEL DOES NOT DO THIS, and this comment said it did until issue
	// #1414. `Simulation.trigger_surge` returns before it logs anything when
	// no city can be landed on, so its schedule does not move and it tries
	// again the next day. The two differ only when nothing can be landed on.
	Surges->RecordSurge(Today, bFromCityFall);

	// A NEW SURGE CYCLE, SO GLOBAL VIGILANCE COUNTS AGAIN FROM NONE: "Resets on new Surge". Every surge comes through
	// here -- the opening one, the clock's, the empty board's and a city's fall -- so every one resets it (Q1).
	ClearsSinceLastSurge = 0;

	// AND A NEW SURGE GIVES BRIBERY ITS ONE BRIBE AGAIN: "Once per Surge", one for the whole run (V12). Slice 9.
	bBribedThisSurge = false;

	OutReport.bSurged = true;

	// AND COUNTED, because one day can hold several since issue #2865: one for each city that falls, and the clock's.
	++OutReport.Surges;
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
		// A QUEST DUNGEON'S TIMER HITS ITS HOST CITY, THEN IT REFRESHES AND MAY
		// MOVE. The owner, 2026-10-08, verbatim: "Quest dungeons will do their
		// city/population damage, and either refresh their timer and stay on
		// that city, or potentially move to an adjacent city." Until
		// 2026-10-13 the timer cost the city nothing and this block began "IT
		// REFRESHES RATHER THAN DETONATING". `docs/DECISIONS.md`, 2026-10-13.
		//
		// THE SIZE IS THE ORDINARY ROW OF THE HOST'S TIER, AT A SCALE OF ONE.
		// `SpecFor(Basic, Host->Tier)`: what an ordinary dungeon of typical
		// depth takes from a city of the tier the Quest dungeon STANDS ON now.
		// Not `Dungeon->CityTier`, which is the tier its depth was rolled from
		// and does not move with it. Not `BiteScale`, so its own floor count
		// does not size the hit. Not its own `DefenceDamage`, which is nought
		// on every Quest row of `SpecFor`. `Simulation._quest_hit` reads the
		// same row.
		//
		// A HOST ALREADY FALLEN IS HIT BY NOTHING, as for an ordinary dungeon.
		//
		// COUNTED IN `QuestHits` AND NOT IN `DungeonsDetonated`. The model
		// counts `quest_hits` apart from `resolved`, and
		// `test_both_halves_count_a_detonation_where_the_city_pays` in
		// `tools/tests/test_surge_port.py` holds `DungeonsDetonated` to the
		// model's `resolved`.
		//
		// THE CLOCK HAS ALREADY REFRESHED THE TIMER -- see
		// `UCataclysmDayClock::AdvanceDay`, which sets every timer that ran
		// out back to full.
		//
		// A FALLEN CITY AND A CATACLYSM REACH THIS BLOCK TOO AND ARE NOT A
		// QUEST DUNGEON, so nothing is taken for them; `RelocateQuestDungeon`
		// below does nothing for them either.
		if (Dungeon->Type == ECataclysmDungeonType::Quest)
		{
			const int32 HostId = Dungeon->CityId;
			const FCataclysmCity* Host = Map->Find(HostId);
			if (Host != nullptr && !Host->bFallen)
			{
				FCataclysmDungeonSpec Ordinary =
					UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Basic, Host->Tier);

				// A HOLLOW ADDS VOID STACKS INSTEAD, since 2026-10-22, and takes
				// no defence and no people: the Void's Quest dungeon, whose hit
				// adds 1 to 3 by how far it has shrunk and never erases a city
				// below eight. `Simulation._quest_hit` makes the same choice in
				// the same place. SO ITS TWO FIGURES ARE NOUGHT, put through the
				// very `Damage` call every Quest hit makes, and its stacks are
				// worked out here, while `Dungeon` and `Host` still point at
				// something. It is still counted, reported and refreshed below
				// exactly as any Quest hit is.
				const bool bHollow = Dungeon->IsHollow();
				const int32 HollowStacks =
					bHollow ? Dungeon->HollowStacksOnto(Host->VoidStacks) : 0;
				if (bHollow)
				{
					Ordinary.DefenceDamage = 0.0f;
					Ordinary.PopulationDamage = 0.0f;
				}

				// AN ASCENDANT ALSO LEAVES ONE OF ITS EDICTS ON THE CITY, since
				// 2026-10-22: one of those it would hold from floor 4, drawn as
				// a walk draws them whether or not it was walked, for 30 days,
				// replacing any earlier one. Its hit is otherwise the ordinary
				// Quest hit. Drawn here, while `Dungeon` still points at it.
				const FName LeftEdict = Dungeon->IsAscendant()
					? UCataclysmDungeonModifierRules::EdictLeftBy(
						  Dungeon->WalkSeed(WalkSeedMask), Dungeon->Modifiers, OutReport.Day)
					: FName(NAME_None);

				++QuestHits;
				OutReport.QuestHits.Add(HostId);

				// FOR THE EMPIRE SCREEN'S ONE LINE. See `LastHitDay`.
				LastHitDay = OutReport.Day;
				LastHitCityId = HostId;
				bLastHitWasAQuestTimer = true;

				// LEFT BEFORE THE HIT IS DEALT, so a hit that fells the city
				// still leaves it there, as a fallen city keeps its stacks.
				if (!LeftEdict.IsNone())
				{
					Map->LeaveEdict(HostId, LeftEdict, OutReport.Day);
				}

				// THE HIT FELLED THE CITY AND THE FALL MOVED THIS DUNGEON, since
				// 2026-10-21: `CityFell` sends a Quest dungeon to the city
				// `PickRefuge` answers rather than absorbing it. It has just hit,
				// the clock has already refilled its timer, and it does not take
				// the timer's own move as well that day -- `Simulation._resolve`
				// refreshes it and returns. A fall that found nowhere to go left
				// it on the fallen city, where the model then tries the timer's
				// move and finds no target either, since nothing qualifies
				// anywhere; so no draw is skipped by returning here. Until
				// 2026-10-21 the fall absorbed it. `Dungeon` may point at nothing
				// now, because the fall took other dungeons off the array.
				//
				// A HOLLOW'S STACKS ON THE SAME LINE, as the ordinary Void
				// dungeon's are below and for the same reason: every `CityFell`
				// in this file follows an `if (Map->Damage(...))` on one line.
				// `Damage` takes nothing from a Hollow's city and answers that
				// it stands; `AddVoidStacks` then fells and erases it at the
				// tenth stack, and the fall moves this dungeon on as any fall
				// does.
				// WHAT THE HIT TAKES, RECORDED as this block is left, by the return below or its end: the city read now and
				// then. Issue #2733 slice 3; changes nothing. See `FHitRecordedOnLeaving` for why not a call after the if.
				// AND THE EMPIRE UPGRADE TREE'S REDUCTIONS, on the figures rather than in the call, so the call stays the
				// one line the port tests read. Read while `Dungeon` still points at it. Issue #2741 slice 3a.
				const FCataclysmTreeDamageFactors Taken = TreeDamageFactorsFor(HostId, Dungeon->Cataclysm);
				Ordinary.DefenceDamage *= Taken.Defence;
				Ordinary.PopulationDamage *= Taken.Population;

				const FHitRecordedOnLeaving RecordTheHit(OutReport, Map.Get(), HostId, ECataclysmCityHitKind::QuestTimer);
				// LAST STAND PROTOCOLS, armed for this one call: a Quest dungeon's timer is a dungeon's resolution (Q3).
				// Issue #2741 slice 3b.
				ArmTheLastStandFor(HostId);
				if (Map->Damage(HostId, Ordinary.DefenceDamage, Ordinary.PopulationDamage) || (bHollow && Map->AddVoidStacks(HostId, HollowStacks)))
				{
					CityFell(HostId, OutReport);
					return;
				}
				NoteTheLastStand();
				NoteTheBailouts(OutReport); // Economic Bailout, issue #2741 slice 9.
			}
		}

		RelocateQuestDungeon(DungeonId, OutReport);
		return;
	}

	const int32 CityId = Dungeon->CityId;

	const FCataclysmCity* City = Map->Find(CityId);
	if (City == nullptr || City->bFallen)
	{
		// NOTHING LEFT TO BITE. The model refreshes the timer and moves on, and
		// the clock has already refreshed it. THE DUNGEON STAYS: it did not
		// hit, so the rule that a dungeon leaves after it hits does not reach
		// it. The model keeps it in the same case.
		return;
	}

	// A DUNGEON ROLLED DEEPER HITS HARDER, in proportion to how deep it was
	// rolled against a typical one of its kind on that tier of city. Floors a
	// city upgrade added or removed are not counted: `BiteScale` reads `Floors`
	// less `UpgradeFloors`.
	//
	// POINTS AND NOT A SHARE OF THE CITY, WHICH IS ISSUE #1331. `Map->Damage`
	// and not `Map->Bite`: the city's own maximum is deliberately not a factor
	// here, so raising a city's ceiling raises how many resolves it survives.
	//
	// A VOID DUNGEON ADDS STACKS INSTEAD, since 2026-10-22: an ordinary
	// dungeon the Void sent adds 1, 2 or 3 Void stacks and takes no defence and
	// no people, and the tenth stack fells the city and erases it. The owner,
	// 2026-10-16, verbatim: "All void dungeons when they resolve add 1-3 void
	// stacks to a city instead of doing damage to the city or population and
	// at 10 stacks the city is erased." `Simulation._resolve` makes the same
	// choice in the same place. SO ITS POINTS ARE NOUGHT, and its stacks are
	// worked out here, while `Dungeon` still points at it.
	const bool bVoid = Dungeon->AddsVoidStacks();
	const int32 Stacks = bVoid ? Dungeon->VoidStacksItAdds() : 0;

	const float Scale = Dungeon->BiteScale();

	// AND THE EMPIRE UPGRADE TREE'S REDUCTIONS: the comment on `UCataclysmEmpireMap::Damage` has always said its caller
	// folds in "any empire tree reduction", and this is it. Issue #2741 slice 3a.
	const FCataclysmTreeDamageFactors Taken = TreeDamageFactorsFor(CityId, Dungeon->Cataclysm);
	const float Defence = bVoid ? 0.0f : Dungeon->DefenceDamage * Scale * Taken.Defence;
	const float Population = bVoid ? 0.0f : Dungeon->PopulationDamage * Scale * Taken.Population;

	// THE EMPIRE IS ABOUT TO PAY, SO THIS IS WHERE IT IS COUNTED. Every return
	// above this line leaves the city untouched -- a kind that does not
	// detonate, or a host that has already fallen -- so a tally raised at the
	// top of this function would count timers rather than damage, which is what
	// `FCataclysmDayReport::Resolved` already gives a caller and the reason
	// nothing could answer how often the empire was hurt. Issue #1324 slice 5.
	//
	// THE MODEL USED TO COUNT THIS DIFFERENTLY AND NOW DOES NOT.
	// `Simulation._resolve` raised `self.resolved` above its own equivalent
	// guard, so a Fallen City dungeon's timer counted there -- measured at 10
	// of 3,665 over thirty campaigns. Issue #1373 moved that line below the
	// guard, so the model's `RunResult.dungeons_resolved` and this are the same
	// number. `test_both_halves_count_a_detonation_where_the_city_pays` in
	// `tools/tests/test_surge_port.py` fails if either half moves back.
	++DungeonsDetonated;

	// ONE HIT AND ONE FALL, BY THE DAMAGE OR BY THE TENTH VOID STACK. A Void
	// dungeon's points are nought, so `Damage` takes nothing from its city and
	// answers that it stands, and its stacks are added after; the tenth fells
	// the city, and `AddVoidStacks` has erased it before answering, so
	// `CityFell` builds no Dungeon City on it -- see `AddFallenCityDungeon`. A
	// Void dungeon is still counted above and still leaves below, exactly as an
	// ordinary dungeon that hit does.
	//
	// ON ONE LINE, AND THE ONE `CityFell`, ON PURPOSE.
	// `tools/tests/test_a_quest_dungeon_moves_when_its_city_falls_in_the_model_and_in_the_game.py`
	// reads every `CityFell` in this file as following an `if (Map->Damage(...))`
	// on one line, which is how it knows the city already reads as fallen when
	// its Quest dungeons look for somewhere to go. `AddVoidStacks` fells the
	// city before it answers, so that still holds.
	//
	// COPIED OUT BEFORE THE BITE. `Damage` and `AddVoidStacks` can lead to
	// `CityFell`, which removes dungeons from `Dungeons`, and `Dungeon` points
	// into that array; nothing here reads it.
	// WHAT THE HIT TAKES, RECORDED: the city read before it and after. Issue #2733 slice 3; changes nothing.
	const FCityBeforeAHit BeforeTheHit = BeforeAHit(Map.Get(), CityId);
	// LAST STAND PROTOCOLS, armed for this one call, an ordinary dungeon's resolution (Q3). Issue #2741 slice 3b.
	ArmTheLastStandFor(CityId);
	if (Map->Damage(CityId, Defence, Population) || (bVoid && Map->AddVoidStacks(CityId, Stacks)))
	{
		CityFell(CityId, OutReport);
	}
	NoteTheLastStand();
	NoteTheBailouts(OutReport); // Economic Bailout, issue #2741 slice 9.
	NoteHitIn(OutReport, Map.Get(), BeforeTheHit, ECataclysmCityHitKind::Ordinary);

	// IT HIT ITS CITY, SO IT LEAVES THE MAP. The owner's rule of 2026-10-08,
	// verbatim: "Ordinary dungeons will do their city/population damage, and
	// then despawn." `Simulation._resolve` pops the dungeon in the same place.
	// Until 2026-10-13 the dungeon stayed with its timer full and hit the same
	// city again each time the timer ran out.
	//
	// THE FALL COMES FIRST, SO THE DUNGEON THAT FELLS ITS CITY IS ABSORBED AND
	// NOT REMOVED HERE. `CityFell` above has already taken every ordinary
	// dungeon on the city off the map, `RemoveDungeon` then answers false, and
	// the day's report names that dungeon in `Absorbed` and not in
	// `HitAndLeft`.
	//
	// `RemoveDungeon` AND NOT `ClearDungeon`. Nobody beat it, so nothing is
	// counted as cleared, no objective is earned and no city is repaired.
	//
	// `RemoveDungeon` ALSO TELLS THE CLOCK THE PLAYER HAS LEFT when this is the
	// dungeon the player is inside, which is how a death reaches this line.
	//
	// ONLY AN ORDINARY DUNGEON ON A CITY THAT WAS STANDING GETS HERE. Every
	// other case returned above.
	if (RemoveDungeon(DungeonId))
	{
		OutReport.HitAndLeft.Add(CityId);

		LastHitDay = OutReport.Day;
		LastHitCityId = CityId;
		bLastHitWasAQuestTimer = false;
	}
}

void UCataclysmEmpireRun::ShrinkHollows(int32 Today)
{
	if (Clock == nullptr)
	{
		return;
	}

	// THE ONE THE PLAYER IS INSIDE IS LEFT AS IT IS. Its walk was fixed when
	// the player went in, and the game mode read its floors then.
	const int32 Inside = Clock->CurrentDungeonId;

	for (FCataclysmDungeon& Dungeon : Dungeons)
	{
		// NOT ON THE DAY IT WAS MADE: a Hollow a surge landed this morning
		// starts shrinking tomorrow, as the model's does.
		if (!Dungeon.IsHollow() || Dungeon.SpawnedDay >= Today
			|| Dungeon.DungeonId == Inside)
		{
			continue;
		}

		const int32 Lose = FMath::Min(
			UCataclysmSurgeScheduler::HollowFloorsLostPerDay,
			Dungeon.HollowFloorsLeftToLose());

		if (Lose <= 0)
		{
			continue;
		}

		Dungeon.Floors -= Lose;

		// THE WALK COMES DOWN BY WHAT THOSE FLOORS COST, AND NEVER BELOW A DAY.
		// NOUGHT MEANS NOBODY SET IT, and then the walk already follows the
		// floors -- see `FCataclysmDungeon::WalkDaysPerFloor` -- so it is left
		// at nought rather than turned into a figure nobody chose.
		if (Dungeon.WalkDays > 0.0f)
		{
			Dungeon.WalkDays = FMath::Max(
				1.0f, Dungeon.WalkDays - Lose * UCataclysmDayClock::DaysPerFloor);
		}

		// AND THE CLOCK'S COPY OF ITS FLOORS, NOT ITS TIMER.
		Clock->SetFloors(Dungeon.DungeonId, Dungeon.Floors);
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

	// ONLY A QUEST DUNGEON MOVES. A Fallen City also answers false to
	// `Resolves` and reaches the caller above, but does not wander: it IS its
	// city. The Cataclysm boss dungeon would answer the same, and since
	// 2026-11-02 its timer never runs out, so it never reaches here; its one
	// move is the Last Stand, issue #43. Checking the kind here rather than at
	// the call site keeps the rule beside the thing it is a rule about.
	if (Dungeon->Type != ECataclysmDungeonType::Quest)
	{
		return;
	}

	const FCataclysmCity* From = Map->Find(Dungeon->CityId);
	if (From == nullptr)
	{
		return;
	}

	// **A FALLEN CITY IS NOT A REASON TO STAY**, since 2026-10-21. A Quest
	// dungeon whose city's fall found it nowhere to go is left standing on the
	// ruin, and when its timer next runs out it tries this move from there:
	// `Simulation._resolve` refreshes it and asks `adjacent_exposed_cities` of
	// the fallen city as of any other. Ruled by the coordinating session,
	// 2026-10-21. Until then this returned for a fallen city as well, which
	// was belt and braces while a fall absorbed every dungeon standing on it.

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
	// the floors the dungeon was rolled with (`Floors` less `UpgradeFloors`)
	// by the midpoint of `SpecFor(Type, CityTier)`. Both halves of
	// that division have to come from the SAME specification row. Moving the
	// tier while leaving `Floors` alone made them come from different rows, so
	// a Quest dungeon that drifted inward onto a bigger city read as shallower
	// than it is and one that drifted outward read as deeper.
	//
	// IT COST NOTHING AND NOTHING WOULD HAVE FAILED WHEN IT DID. `SpecFor`
	// gives a Quest dungeon zero city damage and `Resolves` answers false for
	// it, so `BiteScale` is never reached for the one kind that can move. That
	// is still so since 2026-10-13, when a Quest dungeon's timer began to hit
	// its host: that hit is the ordinary row of the HOST's tier at a scale of
	// one and reads neither `CityTier` nor `BiteScale`. `Simulation._resolve`
	// carries the same fix and the same reasoning, and
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
	// WITH THE SIEGE'S SENDER, which The Sentinel reads. Issue #2741 slice 3a.
	struct FSiegeHost
	{
		int32 CityId = INDEX_NONE;
		int32 DaysStood = 0;
		ECataclysmType Sender = ECataclysmType::None;
	};
	TArray<FSiegeHost> Hosts;

	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
		{
			// NEVER NEGATIVE. A dungeon built by hand in a test may carry a
			// spawn day later than the clock, and a negative age would heal the
			// city rather than hurt it.
			const int32 DaysStood =
				FMath::Max(0, Clock->Day - Dungeon.SpawnedDay);

			Hosts.Add({ Dungeon.CityId, DaysStood, Dungeon.Cataclysm });
		}
	}

	for (const FSiegeHost& Host : Hosts)
	{
		const int32 CityId = Host.CityId;

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
		const float Grown = SiegeDamageGrowthPerDay * Host.DaysStood;

		// THE SHARE IS OF THE MAXIMUM WITH THE EMPIRE UPGRADE TREE, so the tree's raised walls are no better against a
		// Siege than a city upgrade's -- the owner's exception above, kept -- as the simulation's Siege takes its share
		// of a `max_defense` the tree has already raised. The tree's reductions then apply, as the simulation's
		// `damage_taken` does at its Siege. Issue #2741 slice 3a.
		const FCataclysmTreeDamageFactors Taken = TreeDamageFactorsFor(CityId, Host.Sender);

		const float Defence =
			(Map->EffectiveMaxDefence(*City) * SiegeDefenceBitePerDay + Grown) * Taken.Defence;

		const float Population =
			(Map->EffectiveMaxPopulation(*City) * SiegePopulationBitePerDay + Grown) * Taken.Population;

		OutReport.Besieged.Add(CityId);

		// WHAT THE SHARE TAKES, RECORDED: the city read before it and after. Issue #2733 slice 3; changes nothing.
		const FCityBeforeAHit BeforeTheHit = BeforeAHit(Map.Get(), CityId);
		if (Map->Damage(CityId, Defence, Population))
		{
			CityFell(CityId, OutReport);
		}
		NoteTheBailouts(OutReport); // Economic Bailout, issue #2741 slice 9: a Siege's share is a dungeon's damage.
		NoteHitIn(OutReport, Map.Get(), BeforeTheHit, ECataclysmCityHitKind::SiegeShare);
	}
}

void UCataclysmEmpireRun::CityFell(int32 CityId, FCataclysmDayReport& OutReport)
{
	OutReport.Fallen.Add(CityId);

	// THE CITY THAT FELL, READ AFTER THE FALL. `UCataclysmEmpireMap::Fall` has
	// already marked it, so the cities it shielded read as exposed when a
	// Quest dungeon below looks for somewhere to go -- which is the order
	// `Simulation._fall` takes: the city falls, then its dungeons are dealt
	// with.
	const FCataclysmCity* FallenCity = Map != nullptr ? Map->Find(CityId) : nullptr;

	// EVERY ORDINARY DUNGEON STANDING ON IT IS ABSORBED INTO THE ONE THE CITY
	// BECOMES, and how many were absorbed sets both its depth and how many
	// bosses it holds.
	const TArray<int32> Standing = DungeonsOn(CityId);

	int32 DungeonsAbsorbed = 0;

	for (const int32 DungeonId : Standing)
	{
		// LOOKED UP AGAIN FOR EACH ONE, because `RemoveDungeon` below takes
		// dungeons out of the array a pointer from an earlier turn would point
		// into.
		FCataclysmDungeon* Dungeon = Dungeons.FindByPredicate(
			[DungeonId](const FCataclysmDungeon& Candidate)
			{
				return Candidate.DungeonId == DungeonId;
			});

		if (Dungeon == nullptr)
		{
			continue;
		}

		// **A QUEST DUNGEON IS NOT ABSORBED: IT MOVES.** The owner, 2026-10-16,
		// verbatim: "Quest dungeons don't go away when a city falls. They just
		// move to an adjacent city. So eventually they should be hitting
		// something." The simulation took it that day and the game follows
		// since 2026-10-21. It moves at once, with no coin, to the city
		// `PickRefuge` answers -- an adjacent one first, else the nearest that
		// qualifies counted in lanes -- and keeps its floors, its remaining
		// timer and its sub-type. Only `CityId` moves and `CityTier` does not,
		// as on the timer's move; see `RelocateQuestDungeon`.
		//
		// NO CITY FALLS WHILE THE PLAYER IS INSIDE A DUNGEON, since
		// 2026-10-24: the walk's days are run when the player leaves,
		// `SettleDaysOwed`. Nothing here touches the clock, so a Quest dungeon
		// a death left on the map, still named by
		// `UCataclysmDayClock::CurrentDungeonId` while those days run, moves
		// like any other.
		//
		// NOT COUNTED INTO THE DUNGEON CITY. The design's Dungeon City carries
		// "one boss per dungeon that was standing on the city when it fell and
		// was absorbed by it", and a Quest dungeon moves away instead.
		if (Dungeon->Type == ECataclysmDungeonType::Quest)
		{
			if (FallenCity == nullptr)
			{
				continue;
			}

			// THE SIEGES STANDING NOW, COUNTED AGAIN FOR EACH QUEST DUNGEON, the
			// way `RelocateQuestDungeon` counts them. A Quest dungeon an
			// earlier turn of this loop moved is standing on its new city by
			// now, so a second one carrying a Siege is refused that city, as
			// the model's live `sieges_on` refuses it. An empty array for a
			// dungeon carrying none is the "caller did not say" convention.
			const bool bCarriesSiege =
				Dungeon->SubType == ECataclysmDungeonSubType::Siege;

			TArray<int32> SiegesStanding;

			if (bCarriesSiege)
			{
				SiegesStanding.AddZeroed(Map->Cities.Num());

				for (const FCataclysmDungeon& Other : Dungeons)
				{
					if (Other.SubType == ECataclysmDungeonSubType::Siege
						&& SiegesStanding.IsValidIndex(Other.CityId))
					{
						++SiegesStanding[Other.CityId];
					}
				}
			}

			const int32 Refuge = UCataclysmSurgeScheduler::PickRefuge(
				*Map, *FallenCity, Stream, bCarriesSiege, SiegesStanding);

			if (Refuge == INDEX_NONE)
			{
				// NOWHERE QUALIFIES, SO IT STAYS ON THE FALLEN CITY, neither
				// absorbed nor counted. Its timer goes on; when it next runs
				// out it hits nothing and tries the timer's move from here.
				continue;
			}

			Dungeon->CityId = Refuge;

			OutReport.Relocated.Add(DungeonId);
			OutReport.MovedByAFall.Add(DungeonId);
			OutReport.MovedByAFallFrom.Add(CityId);
			OutReport.MovedByAFallTo.Add(Refuge);

			// FOR THE EMPIRE SCREEN'S ONE LINE. See `LastMoveDay`.
			LastMoveDay = OutReport.Day;
			LastMoveFromCityId = CityId;
			LastMoveToCityId = Refuge;

			continue;
		}

		OutReport.Absorbed.Add(DungeonId);
		++DungeonsAbsorbed;

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
	//
	// THE COUNT IS WHAT IT ABSORBED, Quest dungeons left out, since
	// 2026-10-21. It can be nought, when only Quest dungeons stood there, and
	// `MakeFallenCityDungeon` then still gives it one boss, ruled by the
	// coordinating session on 2026-10-21.
	//
	// AFTER THE MOVES ABOVE, SO THEIR DRAWS COME FIRST, as in `Simulation._fall`.
	AddFallenCityDungeon(CityId, DungeonsAbsorbed, OutReport);

	// AND THE FALL FIRES A SURGE OF ITS OWN. The design document says a city
	// falling triggers one; `UCataclysmSurgeScheduler::bCityFallAdvancesEscalation`
	// is what makes it also speed the rest of the run up.
	//
	// EVERY FALL, WHATEVER THE GAP SINCE THE LAST SURGE: the owner's ruling of
	// 2026-11-24 on issue #2865, verbatim "surges from cities falling should
	// happen regardless". So a fall the day after a wave brings another, and
	// two cities falling on one day bring two. From issue #2380 until then
	// `IsDueOnCityFall` refused a fall inside `LeastIntervalDays` (25) of the
	// last surge, as `Simulation._fall` did from issue #1432.
	//
	// AFTER EVERYTHING ABOVE: the city is down, its ordinary dungeons are
	// absorbed, its Quest dungeons have moved and its Dungeon City stands
	// before the wave is rolled, so the wave never targets the fallen city.
	//
	// THE DAY ON THE REPORT, which is today in `AdvanceDay` and the day it
	// already is in `ResolveDungeonOnDeath`.
	if (Surges->IsDueOnCityFall(OutReport.Day))
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

	FCataclysmDungeon Dungeon =
		UCataclysmSurgeScheduler::MakeFallenCityDungeon(
			NextDungeonId, *City, OutReport.Day, DungeonsAbsorbed, Surges->FloorsTheTreeAdds);

	// A FALLEN CITY CARRIES MODIFIERS TOO. The design's rule is about dungeons
	// and names no exception -- "a dungeon carries one modifier per difficulty
	// tier" -- and a Fallen City is a dungeon the player walks. It gets no
	// Cataclysm stamped on it, because nobody sent it; that is a different
	// field and a different rule. Issues #41 and #1324.
	GiveModifiers(Dungeon);

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

FName UCataclysmEmpireRun::LeftEdictCarriedBy(int32 DungeonId) const
{
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	const FCataclysmCity* City =
		(Dungeon != nullptr && Map != nullptr) ? Map->Find(Dungeon->CityId) : nullptr;

	return City != nullptr ? City->EdictCarriedOn(Day()) : FName(NAME_None);
}

TArray<FName> UCataclysmEmpireRun::ModifiersCarriedBy(int32 DungeonId) const
{
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	if (Dungeon == nullptr)
	{
		return TArray<FName>();
	}

	// BEYOND ITS COUNT: after its own, and not in place of one of them.
	TArray<FName> Carried = Dungeon->Modifiers;
	const FName Left = LeftEdictCarriedBy(DungeonId);
	if (!Left.IsNone())
	{
		Carried.AddUnique(Left);
	}
	return Carried;
}

float UCataclysmEmpireRun::ModifierScoreCarriedBy(int32 DungeonId) const
{
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	if (Dungeon == nullptr)
	{
		return 0.0f;
	}

	float Score = Dungeon->ModifierScore;
	const FName Left = LeftEdictCarriedBy(DungeonId);

	// ONLY WHEN IT WAS ADDED: one already among its own is already in its score.
	if (!Left.IsNone() && !Dungeon->Modifiers.Contains(Left))
	{
		for (const FCataclysmDungeonModifier& Row : ModifierPool)
		{
			if (Row.RowKey == Left)
			{
				Score += Row.Danger;
				break;
			}
		}
	}
	return Score;
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
	// AND WHETHER SCOUT PARTIES DISCOVERED IT (issue #2741 slice 9, V16).
	const bool bBonus = Dungeon != nullptr && Dungeon->bBonus;
	LastBonusDungeonAdded = INDEX_NONE;

	// A CALL THAT CLEARS NOTHING AWARDS NOTHING, so the award a caller reads after it is this call's and never the
	// last one's. Issue #52.
	LastEmpirePointsAwarded = 0.0;
	// AND NOTHING REMOVED BY THE WARLORD YET, for the same reason. Issue #2741 slice 4.
	LastDungeonTheWarlordRemoved = INDEX_NONE;

	if (!RemoveDungeon(DungeonId))
	{
		// NOTHING WAS CLEARED, SO NOTHING IS COUNTED. `Kind` above is a
		// placeholder for a dungeon that was not there, and this is the branch
		// that discards it: everything below runs only for a dungeon that
		// really did come off the map.
		return false;
	}

	// THE EMPIRE UPGRADE POINTS THE DEFEAT EARNS, MEASURED NOW: the base for its kind times the share of the empire's
	// population living at this instant. Issue #52, the owner's ruling of 2026-09-06 in `docs/Cataclysm_GDD_v2.md`,
	// Empire-Wide Upgrades.
	//
	// BEFORE `Retake` BELOW AND NOT AFTER. Retaking a Fallen City gives it back half its people, and the share is the
	// empire's at the moment the dungeon was defeated, not after the defeat has been paid for; `Simulation.
	// _finish_current` awards before it calls `_retake`. `tools/tests/test_empire_points_port.py` holds the order.
	LastEmpirePointsAwarded = EmpirePointsFor(Kind,
		Map != nullptr ? static_cast<double>(Map->TotalPopulation()) : 0.0,
		Map != nullptr ? static_cast<double>(Map->TotalMaxPopulation()) : 0.0);
	// AND VETERAN'S EDGE: "+3% Empire XP gained from dungeon clears per point", summed, on every clear's award (W7).
	// After `EmpirePointsFor` and not in it, whose body the port tests hold to the simulation's, which has no such node.
	// Issue #2741 slice 4.
	// AND REINVESTMENT'S, added to it (issue #2741 slice 9, V3).
	LastEmpirePointsAwarded *= 1.0 + static_cast<double>(TreeBonuses.EmpirePointsIncrease)
		+ static_cast<double>(TreeBonuses.EmpirePointsFromSpending);

	const bool bWasFallenCity = Kind == ECataclysmDungeonType::FallenCity;

	// THE RUN REMEMBERS WHAT THE PLAYER BEAT. Issue #1324 slice 5.
	//
	// HERE AND NOT IN `RemoveDungeon`, which is the whole point of the two being
	// separate functions. A city that falls absorbs the dungeons standing on it
	// through `RemoveDungeon`, and nobody walked those -- counting them would
	// pay a player for losing a city and, for a Quest dungeon, would hand them
	// an objective for watching one be destroyed.
	++DungeonsCleared;

	// AND GLOBAL VIGILANCE'S COUNT, every kind as `DungeonsCleared` counts every kind (Q1). Issue #2741 slice 3b.
	++ClearsSinceLastSurge;

	// AND RELENTLESS PURSUIT'S, every kind too, until the next death the run notes (D5). Issue #2741 slice 5b.
	++ClearsSinceLastDeath;

	// ORDINARY DUNGEONS ONLY, WHICH IS A DIFFERENT NUMBER AND NOT A SUBSET
	// KEPT FOR CONVENIENCE. `docs/Cataclysm_GDD_v2.md` section VIII: "Every
	// **ordinary** dungeon defeated adds one floor to the Cataclysm boss
	// dungeon. Quest dungeons and retaken Dungeon Cities do not". So the boss's
	// depth reads this and never `DungeonsCleared`, or pursuing the win
	// condition would make the final fight harder. Issue #1315 is the growth.
	// A SCOUT PARTIES BONUS DUNGEON IS NOT ONE: it "adds no floor to the Cataclysm boss dungeon", and is counted apart
	// so the kinds defeated can leave it out (V16, V17, V19). Issue #2741 slice 9.
	if (bBonus)
	{
		++BonusDungeonsCleared;
	}
	else if (Kind == ECataclysmDungeonType::Basic)
	{
		++BasicDungeonsCleared;

		// AND A BOSS ALREADY STANDING GROWS BY IT, since 2026-11-02: the ordinary
		// dungeons defeated after it opened count as well as those before.
		DeepenCataclysmDungeon();
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

		// AND THE CLEAR THAT MEETS THE REQUIREMENT OPENS THE BOSS DUNGEON, last in
		// this branch, where `Simulation._finish_current` calls
		// `_maybe_open_cataclysm`. Issue #1324 slice 6b.
		OpenCataclysmDungeonIfEarned();
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
		// RECLAIMER'S RESOLVE COUNTS A CITY THAT COMES BACK: one fallen and not erased, which is exactly when `Retake`
		// retakes it (Q2). Issue #2741 slice 3b.
		const FCataclysmCity* Fallen = Map->Find(CityId);
		const bool bCanBeReclaimed = Fallen != nullptr && Fallen->bFallen && !Fallen->bErased;
		Map->Retake(CityId);
		if (bCanBeReclaimed)
		{
			++CitiesReclaimed;

			// STIMULUS PACKAGE'S "UNTIL YOU RECLAIM A CITY" (issue #2741 slice 6b, B2): the next check sets it again if
			// the population is still below half.
			bPopulationFellBelowHalf = false;
		}
	}

	// AND THE CITY IS REPAIRED A LITTLE, IF IT BOUGHT THAT UPGRADE: "When you
	// clear a dungeon, the city's Defense restores by 5%", the whole 5%. A clear
	// gives a city nothing back without it, by the owner's ruling of 2026-11-22,
	// verbatim: "Dungeon clears do not restore city defenses." The sheet said
	// "an additional 5%" until then, and the word was taken out (issue #1267).
	// Neither this class nor `Simulation._clear` in the Python model has a base
	// restore, and neither is to have one.
	if (Map != nullptr && CityId != INDEX_NONE)
	{
		if (FCataclysmCity* City = Map->FindMutable(CityId))
		{
			// AS READ TODAY, so Enduring Foundations strengthens one held long enough. Issue #2741 slice 3b.
			const float Restored = Map->UpgradeValueNowFor(*City,
				ECataclysmCityUpgradeEffect::RestoreDefenceOnClear);

			// A FALLEN CITY IS NOT REPAIRED BY THIS. It has no defence to
			// restore until it is retaken, and letting it climb off zero while
			// still marked fallen would put the map in a state nothing else
			// expects.
			if (Restored > 0.0f && !City->bFallen)
			{
				// OF THE MAXIMUM WITH THE EMPIRE UPGRADE TREE, from what the city is read as holding. Issue #2741 slice 3a.
				const float Max = Map->EffectiveMaxDefence(*City);
				City->Defence = FMath::Min(Max, Map->DefenceOf(*City) + Max * Restored);
			}
		}
	}

	// AND THE WARLORD: "When you clear a dungeon, 15% chance to instantly remove another dungeon in the same city."
	// The coordinating session's W6 of 2026-11-19: the roll takes no draw from the run's streams, so the waves and every
	// seeded test are as they were, and is seeded from the run's walk seed mask and this dungeon's id, so a reload rolls
	// the same; it removes the other dungeon on the city that resolves soonest, never the boss or a Fallen City dungeon;
	// through `RemoveDungeon`, so it pays nothing and is not a clear. THE BOSS IS NEVER ON THE CITY: it stands on none,
	// and a clear of it has no city, so neither reaches this. Issue #2741 slice 4.
	if (TreeBonuses.RemoveAnotherDungeonChance > 0.0f && CityId != INDEX_NONE)
	{
		// Through the mix since issue #2749's placement seeding slice; it was `MixedSeed(...)`, linear, so one run's
		// dungeons drew related rolls.
		FRandomStream Roll(CataclysmSeedMix::Mix(WalkSeedMask ^ (DungeonId + 1), WarlordSalt, 0));
		if (Roll.FRand() < TreeBonuses.RemoveAnotherDungeonChance)
		{
			const int32 Soonest = TheWarlordsDungeonOn(CityId);
			if (Soonest != INDEX_NONE)
			{
				RemoveDungeon(Soonest);
				LastDungeonTheWarlordRemoved = Soonest;
			}
		}
	}

	// SCOUT PARTIES: "+10% chance per point to discover a bonus dungeon after clearing one." Any clear but a bonus
	// dungeon's and the Cataclysm boss's rolls (V15), on its own stream seeded from the run's walk seed mask and this
	// dungeon's id, so no other roll moves and a reload rolls the same; on the same city, while it stands. Slice 9.
	if (!bBonus && TreeBonuses.BonusDungeonChance > 0.0f && CityId != INDEX_NONE) // The boss stands on no city.
	{
		FRandomStream Roll(CataclysmSeedMix::Mix(WalkSeedMask ^ (DungeonId + 1), ScoutSalt, 0));
		if (Roll.FRand() < TreeBonuses.BonusDungeonChance)
		{
			LastBonusDungeonAdded = AddBonusDungeon(CityId, Roll);
		}
	}

	// AND CLEARING THE CATACLYSM'S OWN DUNGEON WINS THE RUN. Issue #1324 slice 6e, ruled by the coordinating session on
	// 2026-11-02: clearing the boss dungeon, through this function as every dungeon is cleared, is the win.
	// `docs/Cataclysm_GDD_v2.md`, Game Start: "The run ends when the player defeats the Cataclysm boss dungeon or loses
	// the capital."
	//
	// LAST, after every count above, as `Simulation._finish_current` sets `won` after it has counted the clear. So the
	// boss is a dungeon cleared like any other before it is a win.
	//
	// THE DAY IS THE DAY THE WALK HAD REACHED, the run's day and the days the walk owed, as a death's day is
	// (`ACataclysmDungeonGameMode::EndTheDungeonForADeath`): the run's own day does not move while the player is inside.
	//
	// AND THE OWED DAYS ARE DROPPED, NOT RUN: the ruling's R6, "the walk's owed days are not run after a win". The run
	// is over, and the character's next run begins at once (`UCataclysmGameInstance::BeginTheRunAfterAWin`), so a day
	// run here would only let a won empire lose cities afterwards. `SettleDaysOwed`, which leaving runs next, then
	// finds none. **THE SIMULATION DIFFERS**: `Simulation._settle_owed` runs them after a win, so that a campaign
	// ends on the day it ended before 2026-10-24.
	if (Kind == ECataclysmDungeonType::Cataclysm)
	{
		bWon = true;
		WonDay = Day() + OwedDays;
		OwedDays = 0; // A win's: the walk's owed days are not run.
	}

	return true;
}

void UCataclysmEmpireRun::NoteLastRunWon(int32 InDay, int32 InTier, ECataclysmType InCataclysm, int32 InPassivePoints)
{
	LastRunWonDay = InDay;
	LastRunWonTier = InTier;
	LastRunWonCataclysm = InCataclysm;
	LastRunWonPassivePoints = InPassivePoints;
}

double UCataclysmEmpireRun::EmpirePointsBaseFor(ECataclysmDungeonType Type)
{
	switch (Type)
	{
		case ECataclysmDungeonType::Basic:      return BasicDungeonEmpirePoints;
		case ECataclysmDungeonType::Quest:      return QuestDungeonEmpirePoints;
		case ECataclysmDungeonType::FallenCity: return FallenCityEmpirePoints;
		case ECataclysmDungeonType::Cataclysm:  return CataclysmDungeonEmpirePoints;
	}

	// A KIND ADDED WITHOUT A BASE IS WORTH AN ORDINARY DUNGEON'S, as `Simulation._finish_current` reads one:
	// `empire_points_per_dungeon.get(d.dtype, 1.0)`.
	return BasicDungeonEmpirePoints;
}

double UCataclysmEmpireRun::EmpirePointsFor(ECataclysmDungeonType Type, double LivingPopulation,
											double MaxPopulation)
{
	// LIVING OVER EVERY CITY'S MAXIMUM, THE DENOMINATOR HELD AT ONE OR MORE, as `World.population_frac` holds it.
	// Linear, no floor and no cap: the owner's ruling of 2026-09-06.
	return EmpirePointsBaseFor(Type) * (LivingPopulation / FMath::Max(1.0, MaxPopulation));
}

// ---------------------------------------------------------------------------
// The empire upgrade tree's city nodes -- issue #2741 slice 3a
// ---------------------------------------------------------------------------

void UCataclysmEmpireRun::SetTreeBonuses(const FCataclysmEmpireTreeBonuses& Bonuses)
{
	TreeBonuses = Bonuses;
	if (Map != nullptr)
	{
		Map->TreeBonuses = Bonuses;
	}
	if (Surges != nullptr)
	{
		Surges->GapBonusDays = Bonuses.SurgeGapDays;
		// AND THE FLOORS IT ADDS TO DUNGEONS MADE FROM NOW ON. Issue #2741 slice 4, W3.
		Surges->FloorsTheTreeAdds = Bonuses.FloorsAddedTo(/*bTheBoss*/ false, ActiveCataclysms.Num());
	}
}

void UCataclysmEmpireRun::ChangeTreeBonuses(const FCataclysmEmpireTreeBonuses& Bonuses)
{
	if (Map == nullptr)
	{
		SetTreeBonuses(Bonuses);
		return;
	}

	// EVERY CITY'S SHARE UNDER THE TREE AS IT WAS, THEN THE TREE AS IT IS, THEN EVERY STANDING CITY PUT BACK AT ITS SHARE
	// OF THE MAXIMA THE NEW TREE GIVES. Issue #2859: "It should maintain the % they were at before buying the upgrades."
	const FCataclysmCityShares Before = Map->SharesNow();
	SetTreeBonuses(Bonuses);
	Map->KeepShares(Before);
}

FCataclysmTreeDamageFactors UCataclysmEmpireRun::TreeDamageFactorsFor(int32 CityId, ECataclysmType Sender) const
{
	const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr;
	if (City == nullptr)
	{
		return FCataclysmTreeDamageFactors();
	}
	FCataclysmTreeDamageFactors Taken = TreeBonuses.DamageFactorsFor(City->Ring(), ActiveCataclysms.Num(),
																	  static_cast<float>(LivingPopulationShare()),
																	  City->Upgrades.Num(),
																	  IsTheCommonestTypeOn(CityId, Sender));

	// AND GLOBAL VIGILANCE AND RECLAIMER'S RESOLVE, which read what this run has done: the clears since the last surge
	// and the cities reclaimed. Every city, defence and population alike (Q1, Q2). Issue #2741 slice 3b.
	const float ByEvents = TreeBonuses.EventDamageFactorFor(ClearsSinceLastSurge, CitiesReclaimed);
	Taken.Defence *= ByEvents;
	Taken.Population *= ByEvents;

	// AND IMPERIAL RESERVE, which reads the gold held (issue #2741 slice 6c, B7): every city, defence and population.
	const float ByGold = TreeBonuses.GoldDamageFactorFor(GoldHeldAtTierOne ? GoldHeldAtTierOne() : 0.0);
	Taken.Defence *= ByGold;
	Taken.Population *= ByGold;
	return Taken;
}

void UCataclysmEmpireRun::ArmTheLastStandFor(int32 CityId)
{
	const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr;
	if (City != nullptr && TreeBonuses.SanctuarySurvivesAt > 0.0f && !bSanctuaryLastStandSpent
		&& City->Tier == ECataclysmCityTier::Sanctuary && City->IsAlive())
	{
		Map->ArmTheLastStand(CityId, TreeBonuses.SanctuarySurvivesAt);
	}
}

int32 UCataclysmEmpireRun::AddBonusDungeon(int32 CityId, FRandomStream& Roll)
{
	const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr;
	if (City == nullptr || !City->IsAlive() || Surges == nullptr || Clock == nullptr)
	{
		return INDEX_NONE; // On a standing city only (V15).
	}

	// ITS SENDER FROM THE ACTIVE CATACLYSMS, drawn from `Roll` and not `CataclysmStream` (V15).
	const ECataclysmType Sender = ActiveCataclysms.Num() > 0
		? ActiveCataclysms[Roll.RandRange(0, ActiveCataclysms.Num() - 1)]
		: ECataclysmType::None;

	// AN ORDINARY DUNGEON OF THE CITY'S TIER, WITH NO SIEGE, ALWAYS BASIC (V18): its roll only places it.
	FCataclysmDungeon Dungeon = Surges->MakeDungeon(NextDungeonId, *City, Day(), Roll, /* bSiegeAllowed */ false,
												   ECataclysmDungeonType::Basic, Sender);
	Dungeon.Cataclysm = Sender;
	Dungeon.bBonus = true;
	GiveModifiersFrom(Dungeon, Roll);

	// THE SAME TWO STEPS A LANDED DUNGEON TAKES, in the same order (`AddFallenCityDungeon`'s reason).
	if (!Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors))
	{
		return INDEX_NONE;
	}
	Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays);
	Dungeons.Add(Dungeon);
	NextDungeonId = FMath::Max(NextDungeonId, Dungeon.DungeonId + 1);
	return Dungeon.DungeonId;
}

bool UCataclysmEmpireRun::LetABonusDungeonLeave(int32 DungeonId)
{
	// "WHEN ITS TIMER RUNS OUT IT LEAVES WITH NO CONSEQUENCE": no damage, no detonation counted, nothing reported as a
	// hit (V16). Apart from `ResolveDungeon`, which is the ordinary dungeon's port of the model and removes it once, last.
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	return Dungeon != nullptr && Dungeon->bBonus && RemoveDungeon(DungeonId);
}

int32 UCataclysmEmpireRun::ThreateningDungeonCount() const
{
	int32 Count = 0;
	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		Count += Dungeon.bBonus ? 0 : 1;
	}
	return Count;
}

int32 UCataclysmEmpireRun::OrdinaryDungeonCount() const
{
	// A BASIC DUNGEON THAT IS NOT A BONUS ONE, and nothing else: issue #2865, the owner's ruling of 2026-11-23.
	int32 Count = 0;
	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		Count += (Dungeon.Type == ECataclysmDungeonType::Basic && !Dungeon.bBonus) ? 1 : 0;
	}
	return Count;
}

FString UCataclysmEmpireRun::RefusalForBribing(int32 DungeonId) const
{
	if (Clock == nullptr || Map == nullptr)
	{
		return TEXT("No run has begun.");
	}
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	if (Dungeon == nullptr)
	{
		return TEXT("There is no such dungeon on the map.");
	}
	if (Dungeon->Type == ECataclysmDungeonType::Cataclysm)
	{
		return TEXT("The Cataclysm's own dungeon cannot be bribed.");
	}
	// ANY DUNGEON WHOSE TIMER RESOLVES (V13): an ordinary one, a Scout Parties bonus one among them.
	if (!Dungeon->Resolves())
	{
		return TEXT("Only a dungeon whose timer resolves can be bribed.");
	}
	if (bBribedThisSurge)
	{
		return TEXT("Bribery has been used this surge; it can be used again after the next surge.");
	}
	const FCataclysmDungeonTimer* Timer = Clock->Timers.FindByPredicate(
		[DungeonId](const FCataclysmDungeonTimer& Candidate) { return Candidate.DungeonId == DungeonId; });
	if (Timer == nullptr)
	{
		return TEXT("That dungeon has no timer to reset.");
	}
	// AT ITS START ALREADY: the stored days are compared, and the tree's bonus days are added on both sides when read.
	if (Timer->DaysUntilResolve >= Timer->ResolveDays)
	{
		return TEXT("Its timer is already at its start.");
	}
	return FString();
}

bool UCataclysmEmpireRun::BribeDungeon(int32 DungeonId)
{
	if (!RefusalForBribing(DungeonId).IsEmpty())
	{
		return false;
	}
	FCataclysmDungeonTimer* Timer = Clock->Timers.FindByPredicate(
		[DungeonId](const FCataclysmDungeonTimer& Candidate) { return Candidate.DungeonId == DungeonId; });
	if (Timer == nullptr)
	{
		return false;
	}
	// "RESET A SINGLE DUNGEON RESOLUTION TIMER TO ITS MAXIMUM VALUE": the value it started at (the ruling of 2026-11-21).
	Timer->DaysUntilResolve = Timer->ResolveDays;
	bBribedThisSurge = true;
	return true;
}

bool UCataclysmEmpireRun::SaveCityFromFalling(int32 CityId)
{
	FCataclysmCity* City = Map != nullptr ? Map->FindMutable(CityId) : nullptr;
	if (City == nullptr || TreeBonuses.CityBailoutPoints <= 0 || City->bFallen || City->bBailedOut
		|| City->Tier == ECataclysmCityTier::Pillar)
	{
		return false;
	}

	// ALL OR NOTHING, FROM THE BALANCE, and the log says why when it is not paid (V9).
	if (!PayTheBailout || !PayTheBailout(*this, CityId, City->Tier))
	{
		return false;
	}

	// HALF ITS MAXIMUM DEFENCE, The Architect's Legacy or not, and its population as it is (V8). A doomed city is saved
	// as any other and stays doomed (V10); only the tenth Void stack dooms one, and that fall never reaches here (V6).
	City = Map->FindMutable(CityId);
	if (City == nullptr)
	{
		return false;
	}
	City->bBailedOut = true;
	City->Defence = Map->EffectiveMaxDefence(*City) * BailoutDefenceFraction;
	return true;
}

void UCataclysmEmpireRun::NoteTheBailouts(FCataclysmDayReport& OutReport)
{
	if (Map != nullptr)
	{
		OutReport.BailedOut.Append(Map->TakeTheBailouts());
	}
}

void UCataclysmEmpireRun::NoteTheLastStand()
{
	if (Map != nullptr && Map->TakeTheLastStandHeld())
	{
		bSanctuaryLastStandSpent = true;
	}
}

void UCataclysmEmpireRun::RunTreeIntervals(int32 Today)
{
	if (Map == nullptr || Today <= 0)
	{
		return;
	}

	// CIVIL ORDER: "Outposts regenerate 3% of their current defense every 30 days per point." Of what each living
	// Outpost holds as the game reads it, to its maximum (Q4, Q7).
	if (TreeBonuses.OutpostRegeneration > 0.0f && Today % FCataclysmEmpireTreeBonuses::CivilOrderIntervalDays == 0)
	{
		for (FCataclysmCity& City : Map->Cities)
		{
			if (City.IsAlive() && City.Tier == ECataclysmCityTier::Outpost)
			{
				const float Held = Map->DefenceOf(City);
				City.Defence = FMath::Min(Map->EffectiveMaxDefence(City),
										  Held + Held * TreeBonuses.OutpostRegeneration);
			}
		}
	}

	// THE MASON'S GUILD: "Every 20 days, the Outpost or Bulwark with the lowest defense restores 20% of its Max Defense
	// and 15% of its Max Population." The living one with the lowest SHARE of its maximum defence, the lowest id on a
	// tie; nothing when that one is full; the same city may be chosen again (Q4, Q5).
	const bool bGuild = TreeBonuses.LowestCityDefenceRestored > 0.0f || TreeBonuses.LowestCityPopulationRestored > 0.0f;
	if (bGuild && Today % FCataclysmEmpireTreeBonuses::MasonsGuildIntervalDays == 0)
	{
		FCataclysmCity* Lowest = nullptr;
		float LowestShare = 0.0f;
		for (FCataclysmCity& City : Map->Cities)
		{
			const bool bReached = City.Tier == ECataclysmCityTier::Outpost || City.Tier == ECataclysmCityTier::Bulwark;
			if (!bReached || !City.IsAlive())
			{
				continue;
			}
			const float Share = Map->DefenceFractionOf(City);
			if (Lowest == nullptr || Share < LowestShare)
			{
				Lowest = &City;
				LowestShare = Share;
			}
		}
		if (Lowest != nullptr && LowestShare < 1.0f)
		{
			const float MaxDefence = Map->EffectiveMaxDefence(*Lowest);
			const float MaxPopulation = Map->EffectiveMaxPopulation(*Lowest);
			Lowest->Defence = FMath::Min(MaxDefence,
										 Map->DefenceOf(*Lowest) + MaxDefence * TreeBonuses.LowestCityDefenceRestored);
			// AND CIVIC PRIDE'S SHARE on the population only (issue #2741 slice 9, V11).
			Lowest->Population = FMath::Min(MaxPopulation, Map->PopulationOf(*Lowest)
												+ MaxPopulation * TreeBonuses.PopulationRestoredShare(
													TreeBonuses.LowestCityPopulationRestored));
		}
	}
}

// ---------------------------------------------------------------------------
// The empire upgrade tree's walk -- issue #2741 slice 4
// ---------------------------------------------------------------------------

float UCataclysmEmpireRun::EffectiveWalkDaysFor(const FCataclysmDungeon& Dungeon) const
{
	// THE STORED FIGURE: the depth less any city upgrade, never below one day; or, where nothing stored one -- a Fallen
	// City, a dungeon built by hand -- one day a floor, which is what `WalkDaysPerFloor` charges then.
	const float Stored = Dungeon.WalkDays > 0.0f
		? Dungeon.WalkDays
		: static_cast<float>(FMath::Max(1, Dungeon.Floors)) * UCataclysmDayClock::DaysPerFloor;

	// NO WALK NODE, OR A COW LEVEL, WHOSE "Time to complete is doubled and cannot be reduced": the stored figure, exactly.
	if (!TreeBonuses.ReachesTheWalk() || Dungeon.SubType == ECataclysmDungeonSubType::CowLevel)
	{
		return Stored;
	}

	// THE LAST STAND OPTION: one day, replacing every other reduction (W5).
	if (TreeBonuses.OneDayWalkWithinDaysOfFalling > 0 && Dungeon.CityId != INDEX_NONE
		&& IsWithinDaysOfFalling(Dungeon.CityId, TreeBonuses.OneDayWalkWithinDaysOfFalling))
	{
		return 1.0f;
	}

	// THE FLAT DAYS FIRST (W1): The Delver; Opportunist when no other dungeon stands on the city (W9); Sovereign's Haste
	// for each active type, to its 30 (W8); Imperial Roads on a Fallen City dungeon; Rapid Descent for each floor
	// cleared before the last (W2).
	float Flat = TreeBonuses.WalkDaysFewer;
	if (TreeBonuses.WalkDaysFewerWhenAlone > 0.0f && Dungeon.CityId != INDEX_NONE)
	{
		const bool bAlone = !Dungeons.ContainsByPredicate(
			[&Dungeon](const FCataclysmDungeon& Other)
			{
				return Other.CityId == Dungeon.CityId && Other.DungeonId != Dungeon.DungeonId;
			});
		if (bAlone)
		{
			Flat += TreeBonuses.WalkDaysFewerWhenAlone;
		}
	}
	const float PerType = TreeBonuses.WalkDaysFewerPerActiveType * static_cast<float>(ActiveCataclysms.Num());
	Flat += TreeBonuses.WalkDaysFewerPerActiveTypeCap > 0.0f
		? FMath::Min(PerType, TreeBonuses.WalkDaysFewerPerActiveTypeCap)
		: PerType;
	if (Dungeon.Type == ECataclysmDungeonType::FallenCity)
	{
		Flat += TreeBonuses.WalkDaysFewerOnFallenCities;
	}
	Flat += TreeBonuses.WalkDaysFewerPerFloorCleared * static_cast<float>(FMath::Max(0, Dungeon.Floors - 1));

	// THEN THE SHARE LEFT, THEN TACTICAL ENTRY'S HALF ABOVE ITS FLOORS, THEN THE ONE-DAY MINIMUM (W1).
	float Days = (Stored - Flat) * TreeBonuses.WalkTimeFactor;
	if (TreeBonuses.HalveWalkAboveFloors > 0 && Dungeon.Floors > TreeBonuses.HalveWalkAboveFloors)
	{
		Days *= 0.5f;
	}
	return FMath::Max(1.0f, Days);
}

float UCataclysmEmpireRun::WalkDaysPerFloorFor(const FCataclysmDungeon& Dungeon) const
{
	return EffectiveWalkDaysFor(Dungeon) / static_cast<float>(FMath::Max(1, Dungeon.Floors));
}

float UCataclysmEmpireRun::ScheduledDefenceLossOn(int32 CityId, int32 Days) const
{
	const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr;
	if (City == nullptr || City->bFallen || Clock == nullptr)
	{
		return 0.0f;
	}

	float Loss = 0.0f;
	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		// A SCOUT PARTIES BONUS DUNGEON TAKES NOTHING when it leaves (V16). Issue #2741 slice 9.
		if (Dungeon.CityId != CityId || Dungeon.bBonus)
		{
			continue;
		}
		const float Taken = TreeDamageFactorsFor(CityId, Dungeon.Cataclysm).Defence;

		// A TIMER THAT RUNS OUT INSIDE THE DAYS: one hit, as `ResolveDungeon` deals it. An ordinary dungeon's own damage
		// scaled by its depth, a Quest dungeon's the ordinary row of its city's tier; a Void dungeon or a Hollow takes
		// no defence. Once each: an ordinary dungeon leaves after it hits, and a Quest dungeon moves on.
		if (Clock->DaysUntilResolveFor(Dungeon.DungeonId) <= static_cast<float>(Days))
		{
			if (Dungeon.Resolves() && !Dungeon.AddsVoidStacks())
			{
				Loss += Dungeon.DefenceDamage * Dungeon.BiteScale() * Taken;
			}
			else if (Dungeon.Type == ECataclysmDungeonType::Quest && !Dungeon.IsHollow())
			{
				Loss += UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Basic, City->Tier).DefenceDamage
					* Taken;
			}
		}

		// A SIEGE'S DAILY SHARE FOR EACH OF THOSE DAYS, growing as it stands, as `ApplySiegeDamage` takes it.
		if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
		{
			for (int32 Ahead = 1; Ahead <= Days; ++Ahead)
			{
				const int32 DaysStood = FMath::Max(0, Clock->Day + Ahead - Dungeon.SpawnedDay);
				Loss += (Map->EffectiveMaxDefence(*City) * SiegeDefenceBitePerDay
						 + SiegeDamageGrowthPerDay * DaysStood) * Taken;
			}
		}
	}

	// AND THE CITY'S OWN RESISTANCE, as `UCataclysmEmpireMap::Damage` takes it off every hit.
	const float Resisted = FMath::Clamp(Map->UpgradeValueNowFor(*City, ECataclysmCityUpgradeEffect::ResistDefenceLoss),
										0.0f, 1.0f);
	return Loss * (1.0f - Resisted);
}

int32 UCataclysmEmpireRun::TheWarlordsDungeonOn(int32 CityId) const
{
	// THE SOONEST TO RESOLVE, as the city upgrade that removes dungeons takes them; NEVER A FALLEN CITY DUNGEON, the
	// city's own retaking (W6). A function of its own so `ClearDungeon` names each kind once, which
	// `tools/tests/test_surge_port.py` holds.
	int32 Soonest = INDEX_NONE;
	float SoonestDays = 0.0f;
	for (const FCataclysmDungeon& Other : Dungeons)
	{
		if (Other.CityId != CityId || Other.Type == ECataclysmDungeonType::FallenCity || Clock == nullptr)
		{
			continue;
		}
		const float Days = Clock->DaysUntilResolveFor(Other.DungeonId);
		if (Soonest == INDEX_NONE || Days < SoonestDays)
		{
			Soonest = Other.DungeonId;
			SoonestDays = Days;
		}
	}
	return Soonest;
}

bool UCataclysmEmpireRun::IsWithinDaysOfFalling(int32 CityId, int32 Days) const
{
	const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr;
	return City != nullptr && City->IsAlive() && ScheduledDefenceLossOn(CityId, Days) >= Map->DefenceOf(*City);
}

bool UCataclysmEmpireRun::IsTheCommonestTypeOn(int32 CityId, ECataclysmType Sender) const
{
	if (Sender == ECataclysmType::None)
	{
		return false;
	}

	TMap<ECataclysmType, int32> Counts;
	for (const FCataclysmDungeon& Dungeon : Dungeons)
	{
		if (Dungeon.CityId == CityId && Dungeon.Cataclysm != ECataclysmType::None)
		{
			++Counts.FindOrAdd(Dungeon.Cataclysm);
		}
	}

	int32 Most = 0;
	for (const TPair<ECataclysmType, int32>& Count : Counts)
	{
		Most = FMath::Max(Most, Count.Value);
	}
	const int32* Senders = Counts.Find(Sender);
	return Senders != nullptr && *Senders == Most;
}

float UCataclysmEmpireRun::ResolveBonusDaysFor(int32 DungeonId) const
{
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	const FCataclysmCity* City = (Dungeon != nullptr && Map != nullptr) ? Map->Find(Dungeon->CityId) : nullptr;
	if (City == nullptr)
	{
		return 0.0f;
	}
	return TreeBonuses.ResolveDaysFor(City->Tier, Map->DefenceFractionOf(*City));
}

double UCataclysmEmpireRun::LivingPopulationShare() const
{
	if (Map == nullptr)
	{
		return 0.0;
	}

	return static_cast<double>(Map->TotalPopulation())
		/ FMath::Max(1.0, static_cast<double>(Map->TotalMaxPopulation()));
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
// A death inside a dungeon -- issue #41
// ---------------------------------------------------------------------------

FCataclysmDayReport UCataclysmEmpireRun::ResolveDungeonOnDeath(int32 DungeonId)
{
	FCataclysmDayReport Report;

	if (Map == nullptr || Clock == nullptr || Surges == nullptr)
	{
		// `Begin` HAS NOT RUN. The same answer `AdvanceDay` gives, for its
		// reason: an empty report is better than reading through a null map.
		return Report;
	}

	// NO DAY PASSES HERE, so the report carries the day it already is. A city
	// this fells lands its Dungeon City on that day, and its surge when one is
	// due on that day, which is what `CityFell` reads off the report.
	Report.Day = Clock->Day;

	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	if (Dungeon == nullptr || !Dungeon->Resolves())
	{
		// NOT THERE, OR A KIND THAT DOES NOT RESOLVE. Nothing is touched, and
		// `ResolveDungeon` is deliberately not called: for a Quest dungeon it
		// runs the relocation, which belongs to the timer running out and draws
		// from the run's chance. See the header.
		return Report;
	}

	// A SCOUT PARTIES BONUS DUNGEON LEAVES INSTEAD, with no consequence and nothing resolved (issue #2741 slice 9, V16).
	if (LetABonusDungeonLeave(DungeonId))
	{
		return Report;
	}

	Report.Resolved.Add(DungeonId);

	// THE SAME FUNCTION A TIMER RUNNING OUT REACHES, so the consequence, the
	// count and a city falling are one rule and not two.
	//
	// `Dungeon` IS NOT READ AGAIN BELOW. A resolve can fell the city, and
	// `CityFell` removes every dungeon standing on it from `Dungeons`, this one
	// included, so the pointer may no longer point at anything.
	ResolveDungeon(DungeonId, Report);

	// AND STIMULUS PACKAGE'S CHECK, AFTER THE DEATH'S BITE (issue #2741 slice 6b, B2).
	NotePopulationAgainstHalf();

	// THE REFILL, KEPT FOR THE ONE CASE IT STILL SERVES. Since 2026-10-13 an
	// ordinary dungeon that hits its city leaves the map inside
	// `ResolveDungeon`, timer and all, and one whose hit fells the city is
	// absorbed there. In both cases no timer is found here and this does
	// nothing, which is the right answer for a dungeon that is gone.
	//
	// THE CASE LEFT IS AN ORDINARY DUNGEON STANDING ON A CITY THAT HAD ALREADY
	// FALLEN. `ResolveDungeon` returns for it before any damage and before the
	// removal, so it stays on the map, and its timer is set back to full here
	// exactly as `UCataclysmDayClock::AdvanceDay` leaves a timer that ran out.
	// Without that the days a death costs would be counted against whatever
	// the timer had left. A test places such a dungeon by hand; whether play
	// can put one there was not established. Kept by ruling, 2026-10-13.
	//
	// LOOKED UP NOW AND NOT BEFORE THE RESOLVE, for the reason above.
	FCataclysmDungeonTimer* Timer = Clock->Timers.FindByPredicate(
		[DungeonId](const FCataclysmDungeonTimer& Candidate)
		{
			return Candidate.DungeonId == DungeonId;
		});

	if (Timer != nullptr)
	{
		++Timer->TimesResolved;
		Timer->DaysUntilResolve = Timer->ResolveDays;
	}

	Report.bPillarExposed = Map->IsPillarExposed();

	return Report;
}

int32 UCataclysmEmpireRun::DeathDayCost() const
{
	// STANDARD FOR A RUN THAT HAS NOT BEGUN, which is what `DeathDayCostFor`
	// answers for any rung nobody chose.
	return UCataclysmDayClock::DeathDayCostFor(
		Surges != nullptr ? Surges->LethalityRung : 0);
}

FString UCataclysmEmpireRun::NameOfDungeon(int32 DungeonId) const
{
	const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);
	if (Dungeon == nullptr)
	{
		return FString();
	}

	// THE SUB-TYPE, THEN THE KIND, THEN "dungeon": "the Horde Quest dungeon". Either word is left out when it says
	// nothing, which `SubTypeName` and `KindName` answer for `None` and `Basic`, so most dungeons are "the dungeon".
	TArray<FString> Words;
	const FString SubType = UCataclysmSurgeScheduler::SubTypeName(Dungeon->SubType);
	if (!SubType.IsEmpty())
	{
		Words.Add(SubType);
	}
	const FString Kind = UCataclysmSurgeScheduler::KindName(Dungeon->Type);
	if (!Kind.IsEmpty())
	{
		Words.Add(Kind);
	}
	Words.Add(TEXT("dungeon"));
	const FString Called = TEXT("the ") + FString::Join(Words, TEXT(" "));

	// AND THE CITY IT STANDS ON, by the map's own name for it. A dungeon whose city cannot be found is still named.
	const FCataclysmCity* City = Map != nullptr ? Map->Find(Dungeon->CityId) : nullptr;
	return City != nullptr ? FString::Printf(TEXT("%s on %s"), *Called, *City->Name) : Called;
}

void UCataclysmEmpireRun::NoteDaysAway(int32 FromDay, int32 ToDay, const TArray<FCataclysmDayReport>& Reports,
									   bool bEndedInAClear)
{
	FCataclysmDaysAway Away;
	Away.FromDay = FromDay;
	Away.ToDay = ToDay;
	Away.bEndedInAClear = bEndedInAClear;
	for (const FCataclysmDayReport& Day : Reports)
	{
		// EVERY HIT THAT TOOK SOMETHING OR FELLED A CITY, summed by city. A hit that took nothing -- a Hollow's, which
		// adds Void stacks and takes no points, or one on a city already fallen -- is not a city the player lost
		// anything in, and the line says nothing of it.
		for (const FCataclysmCityHit& Hit : Day.Hits)
		{
			if (Hit.DefenceLost <= 0.0f && Hit.PopulationLost <= 0.0f && !Hit.bFelledTheCity)
			{
				continue;
			}
			FCataclysmCityLosses* City = Away.Cities.FindByPredicate(
				[&Hit](const FCataclysmCityLosses& Each) { return Each.CityId == Hit.CityId; });
			if (City == nullptr)
			{
				City = &Away.Cities.AddDefaulted_GetRef();
				City->CityId = Hit.CityId;
			}
			City->DefenceLost += Hit.DefenceLost;
			City->PopulationLost += Hit.PopulationLost;
		}
		for (const int32 CityId : Day.Fallen)
		{
			Away.Fallen.AddUnique(CityId);
		}
		Away.Surges += Day.Surges;
		Away.SurgeDungeons += Day.SurgeDungeons;
	}
	LastDaysAway = Away;
}

void UCataclysmEmpireRun::NoteClear(int32 Day, const FString& DungeonName, int32 WalkDays)
{
	LastClearDay = Day;
	LastClearDungeonName = DungeonName;
	LastClearWalkDays = WalkDays;
}

void UCataclysmEmpireRun::NoteDeath(int32 Day, const FString& DungeonName, int32 WalkDays, int32 DaysCost,
									const TArray<int32>& FallenCityIds)
{
	LastDeathDay = Day;
	LastDeathDungeonName = DungeonName;
	LastDeathWalkDays = WalkDays;
	LastDeathDaysCost = DaysCost;
	LastDeathFallenCityIds = FallenCityIds;

	// AND RELENTLESS PURSUIT'S CLEARS IN A ROW END HERE, at every death the run hears of (D5). Issue #2741 slice 5b.
	ClearsSinceLastDeath = 0;
}

// ---------------------------------------------------------------------------
// City upgrades
// ---------------------------------------------------------------------------

ECataclysmCityUpgradeResult UCataclysmEmpireRun::WouldBuyCityUpgrade(
	int32 CityId, const FCataclysmCityUpgrade& Upgrade, int64 GoldCost,
	int64 GoldHeld) const
{
	if (Map == nullptr || Clock == nullptr)
	{
		return ECataclysmCityUpgradeResult::RunHasNotBegun;
	}

	if (!Upgrade.IsValid())
	{
		return ECataclysmCityUpgradeResult::NotAnUpgrade;
	}

	if (!UCataclysmCityUpgradeRules::IsBuilt(Upgrade.Effect))
	{
		// REFUSED RATHER THAN SOLD. Five of the 24 upgrades are waiting on a
		// system that does not exist, and a slot spent on one of those would buy
		// the player nothing at all.
		return ECataclysmCityUpgradeResult::EffectNotBuiltYet;
	}

	// THE CLEANSE'S TWO GATES, issue #2838, ruled 2026-11-22. Both are the run's and not the city's, so they come
	// before anything about the city: every city gives the same answer.
	if (Upgrade.Effect == ECataclysmCityUpgradeEffect::CleanseEveryCity)
	{
		// "will only be available on T3 and above": the difficulty tier, ruled 2026-11-22.
		if (DifficultyTier < UCataclysmCityUpgradeRules::CleanseLowestDifficultyTier)
		{
			return ECataclysmCityUpgradeResult::BelowItsDifficultyTier;
		}
		// "Can only purchase once": once a run, on any city.
		if (bCleanseBoughtThisRun)
		{
			return ECataclysmCityUpgradeResult::AlreadyBoughtThisRun;
		}
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
	// THE OTHER HALF, A SIEGE PAUSING A BUILD UNDER WAY, is `AdvanceCityBuilds`,
	// since issue #2825.
	//
	// CHECKED BEFORE THE SLOT COUNT, so a player asking about a besieged city
	// that is also full is told the thing they can actually act on.
	if (IsBesieged(CityId))
	{
		return ECataclysmCityUpgradeResult::CityIsBesieged;
	}

	// AN UPGRADE BEING BUILT IS ALREADY BOUGHT: its slot is taken and it is
	// finished when its days are counted. Issue #2825.
	if (City->HasUpgrade(Upgrade.RowName) || City->IsBuilding(Upgrade.RowName))
	{
		return ECataclysmCityUpgradeResult::AlreadyBought;
	}

	if (Map->FreeUpgradeSlots(CityId) <= 0)
	{
		return ECataclysmCityUpgradeResult::NoSlotsLeft;
	}

	// ONE BUILD AT A TIME PER CITY, the coordinating session's ruling of
	// 2026-11-21 (issue #2825). After the slot count, so a city whose last slot
	// is the one being built says it is full.
	if (!City->Building.IsEmpty())
	{
		return ECataclysmCityUpgradeResult::BuildUnderWay;
	}

	// THE PRICE, LAST, so a refusal for anything above -- which no amount of
	// gold would change -- is the one a player is told. Both numbers are the
	// `Cataclysm` module's and are passed in: the gold is held by the account
	// record or a Solo Self-Found character, which this module cannot see. A
	// price of nought is free whatever is held. Issue #2742's second slice.
	if (GoldCost > 0 && GoldHeld < GoldCost)
	{
		return ECataclysmCityUpgradeResult::NotEnoughGold;
	}

	return ECataclysmCityUpgradeResult::Bought;
}

ECataclysmCityUpgradeResult UCataclysmEmpireRun::BuyCityUpgrade(
	int32 CityId, const FCataclysmCityUpgrade& Upgrade, int64 GoldCost,
	int64 GoldHeld, int32 BuildDays)
{
	// EVERY CHECK IS ABOVE, IN ONE PLACE. The city screen greys out what a city
	// cannot buy and says why, and it asks exactly this question rather than
	// deciding for itself, so the screen and the purchase cannot drift apart.
	const ECataclysmCityUpgradeResult Refusal =
		WouldBuyCityUpgrade(CityId, Upgrade, GoldCost, GoldHeld);

	if (Refusal != ECataclysmCityUpgradeResult::Bought)
	{
		return Refusal;
	}

	FCataclysmCityUpgrade Bought = Upgrade;
	Bought.Tier = 1;

	// THE CLEANSE IS SPENT FOR THE RUN AT PURCHASE, not when it is finished, so no second city buys it while this one
	// builds it (issue #2838).
	if (Bought.Effect == ECataclysmCityUpgradeEffect::CleanseEveryCity)
	{
		bCleanseBoughtThisRun = true;
	}

	// IT TAKES DAYS TO BUILD, since issue #2825: the slot is taken now, and the
	// upgrade does nothing until `AdvanceCityBuilds` has counted its days. The
	// gold was taken by the caller already; a build cannot be cancelled and
	// nothing is refunded (ruled 2026-11-21).
	if (BuildDays > 0)
	{
		Bought.BuildDaysLeft = BuildDays;
		Map->StartBuild(CityId, Bought);
		return ECataclysmCityUpgradeResult::Bought;
	}

	// NOUGHT DAYS IS FINISHED AT ONCE, which is what the empire layer's own
	// tests buy, as they buy free.
	CompleteCityUpgrade(CityId, Bought, Clock->Day);
	return ECataclysmCityUpgradeResult::Bought;
}

ECataclysmCityUpgradeResult UCataclysmEmpireRun::WouldRaiseCityUpgrade(
	int32 CityId, const FCataclysmCityUpgrade& Raised, int64 GoldCost, int64 GoldHeld) const
{
	if (Map == nullptr || Clock == nullptr)
	{
		return ECataclysmCityUpgradeResult::RunHasNotBegun;
	}
	if (!Raised.IsValid())
	{
		return ECataclysmCityUpgradeResult::NotAnUpgrade;
	}
	if (!UCataclysmCityUpgradeRules::IsBuilt(Raised.Effect))
	{
		return ECataclysmCityUpgradeResult::EffectNotBuiltYet;
	}
	// THE CLEANSE HAS NO LADDER: its tier 2 and 3 cells are empty (issue #1265, derived).
	if (Raised.Effect == ECataclysmCityUpgradeEffect::CleanseEveryCity)
	{
		return ECataclysmCityUpgradeResult::NoTierToRaiseTo;
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
	if (IsBesieged(CityId))
	{
		return ECataclysmCityUpgradeResult::CityIsBesieged;
	}

	const FCataclysmCityUpgrade* Held = City->Upgrades.FindByPredicate(
		[&Raised](const FCataclysmCityUpgrade& Upgrade) { return Upgrade.RowName == Raised.RowName; });
	if (Held == nullptr)
	{
		// BEING BUILT AS A PURCHASE IS NOT YET HELD: it is the build under way that stops a raise.
		return City->IsBuilding(Raised.RowName) ? ECataclysmCityUpgradeResult::BuildUnderWay
												: ECataclysmCityUpgradeResult::NotHeld;
	}
	if (Held->Tier >= UCataclysmCityUpgradeRules::HighestTier)
	{
		return ECataclysmCityUpgradeResult::AtTheTopTier;
	}
	// ONE TIER AT A TIME: a raise asked for any tier but the next is not one this game sells.
	if (Raised.Tier != Held->Tier + 1)
	{
		return ECataclysmCityUpgradeResult::NotAnUpgrade;
	}
	// THE CITY'S ONE BUILD AT A TIME (Q8, derived from issue #2825's rule).
	if (!City->Building.IsEmpty())
	{
		return ECataclysmCityUpgradeResult::BuildUnderWay;
	}
	if (GoldCost > 0 && GoldHeld < GoldCost)
	{
		return ECataclysmCityUpgradeResult::NotEnoughGold;
	}
	return ECataclysmCityUpgradeResult::Bought;
}

ECataclysmCityUpgradeResult UCataclysmEmpireRun::RaiseCityUpgrade(int32 CityId, const FCataclysmCityUpgrade& Raised,
																   int64 GoldCost, int64 GoldHeld, int32 BuildDays)
{
	const ECataclysmCityUpgradeResult Refusal = WouldRaiseCityUpgrade(CityId, Raised, GoldCost, GoldHeld);
	if (Refusal != ECataclysmCityUpgradeResult::Bought)
	{
		return Refusal;
	}

	// ON THE BUILD LIST AS THE UPGRADE AT ITS NEW TIER, while the one held goes on acting at its old tier (Q7).
	FCataclysmCityUpgrade Build = Raised;
	if (BuildDays > 0)
	{
		Build.BuildDaysLeft = BuildDays;
		Map->StartBuild(CityId, Build);
		return ECataclysmCityUpgradeResult::Bought;
	}
	CompleteCityUpgrade(CityId, Build, Clock->Day);
	return ECataclysmCityUpgradeResult::Bought;
}

const FCataclysmCityUpgrade* UCataclysmEmpireRun::BuildOn(int32 CityId) const
{
	const FCataclysmCity* City = Map != nullptr ? Map->Find(CityId) : nullptr;
	return City != nullptr && !City->Building.IsEmpty() ? &City->Building[0] : nullptr;
}

bool UCataclysmEmpireRun::IsBuildPaused(int32 CityId) const
{
	if (BuildOn(CityId) == nullptr)
	{
		return false;
	}
	const FCataclysmCity* City = Map->Find(CityId);
	return City->bFallen || IsBesieged(CityId);
}

int32 UCataclysmEmpireRun::BuildReadyDay(int32 CityId) const
{
	const FCataclysmCityUpgrade* Build = BuildOn(CityId);
	if (Build == nullptr || Clock == nullptr || IsBuildPaused(CityId))
	{
		return INDEX_NONE;
	}
	return Clock->Day + Build->BuildDaysLeft;
}

void UCataclysmEmpireRun::AdvanceCityBuilds(int32 Today, FCataclysmDayReport& Report)
{
	if (Map == nullptr)
	{
		return;
	}

	for (FCataclysmCity& City : Map->Cities)
	{
		// PAUSED: A FALLEN CITY BUILDS NOTHING, AND NEITHER DOES A BESIEGED ONE. The build keeps its days left and goes
		// on when the city is retaken or the Siege is gone (ruled 2026-11-21; the Siege by the owner, 2026-09-05).
		if (City.Building.IsEmpty() || City.bFallen || IsBesieged(City.CityId))
		{
			continue;
		}

		// TAKEN OFF THE LIST BEFORE IT IS APPLIED, so the city holds it once: in `Upgrades`, never in both.
		TArray<FCataclysmCityUpgrade> Finished;
		for (int32 Index = City.Building.Num() - 1; Index >= 0; --Index)
		{
			FCataclysmCityUpgrade& Build = City.Building[Index];
			Build.BuildDaysLeft = FMath::Max(0, Build.BuildDaysLeft - 1);
			if (Build.BuildDaysLeft == 0)
			{
				Finished.Insert(Build, 0);
				City.Building.RemoveAt(Index);
			}
		}

		// THE CITY ID IS COPIED, because finishing an upgrade can remove dungeons, and nothing below may hold a
		// reference into the map across it.
		const int32 CityId = City.CityId;
		for (const FCataclysmCityUpgrade& Done : Finished)
		{
			CompleteCityUpgrade(CityId, Done, Today);
			Report.UpgradesFinishedOn.Add(CityId);
		}
	}
}

void UCataclysmEmpireRun::CompleteCityUpgrade(int32 CityId, FCataclysmCityUpgrade Bought, int32 Day)
{
	Bought.BuildDaysLeft = 0;

	// A RAISE, WHEN THE CITY ALREADY HOLDS THE ROW: a purchase of a row the city holds or is building is refused, so a
	// finished build of a held row is a raise (issue #1265). The map raises it in place and keeps the day it was first
	// finished.
	const FCataclysmCity* Holder = Map->Find(CityId);
	if (Holder != nullptr && Holder->HasUpgrade(Bought.RowName))
	{
		Map->RaiseUpgrade(CityId, Bought, Day);
	}
	else
	{
		// THE DAY IT WAS FINISHED, which Enduring Foundations reads: an upgrade being built does nothing, so it has
		// been "active" only from today. Issue #2741 slice 3b; finished rather than bought since issue #2825.
		Bought.BoughtDay = Day;

		// THE FIRST TRIGGER IS A FULL INTERVAL AWAY. An upgrade finished on day 37
		// with a 20 day interval first fires on day 57, rather than on whichever
		// coming day happens to divide by 20. Zero for every effect that is not one
		// of the two interval ones, and unread for them.
		if (Bought.IntervalDays > 0.0f)
		{
			Bought.NextTriggerDay =
				Day + FMath::Max(1, FMath::RoundToInt(Bought.IntervalDays));
		}

		// THE MAP RECORDS IT AND APPLIES WHAT BELONGS TO THE CITY: the two raised
		// maxima and the two restores. Everything below here is what the map cannot
		// do, because it involves dungeons.
		Map->AddUpgrade(CityId, Bought);
	}

	// A PURCHASE OR A RAISE OF IT: a raise fires it again at the new tier's full figure (Q9, issue #1265).
	if (Bought.Effect == ECataclysmCityUpgradeEffect::RemoveDungeons)
	{
		RemoveShareOfDungeonsOn(CityId, Bought.Value);
	}

	if (Bought.Effect == ECataclysmCityUpgradeEffect::CleanseEveryCity)
	{
		CleanseEveryCity();
	}
}

void UCataclysmEmpireRun::RemoveShareOfDungeonsOn(int32 CityId, float Share)
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
		FMath::RoundToInt(ByUrgency.Num() * Share),
		0, ByUrgency.Num());

	for (int32 Index = 0; Index < ToRemove; ++Index)
	{
		// `RemoveDungeon` AND NOT `ClearDungeon`. Nobody walked these; they
		// were dispersed by building work, so they must not pay out what
		// beating a dungeon pays out.
		RemoveDungeon(ByUrgency[Index]);
	}
}

void UCataclysmEmpireRun::CleanseEveryCity()
{
	if (Map == nullptr || Clock == nullptr)
	{
		return;
	}

	// THE CITY IDS ARE COPIED FIRST, because removing a dungeon can change the map, and nothing below holds a reference
	// into it across a removal.
	TArray<int32> CityIds;
	for (const FCataclysmCity& City : Map->Cities)
	{
		// EVERY CITY THAT HAS NOT FALLEN, the buying city included (ruling 4). A fallen city has no defence or
		// population to lose, and its dungeons are its own retaking.
		if (!City.bFallen)
		{
			CityIds.Add(City.CityId);
		}
	}

	for (const int32 CityId : CityIds)
	{
		// THE DUNGEONS COUNTED: neither a Quest dungeon nor a Fallen City dungeon, as the sheet says ("excluding Quest
		// and Fallen City dungeons"), nor the Cataclysm's own dungeon. The sheet does not name that one; leaving it out
		// is DERIVED, confirmed by the coordinating session on 2026-11-22: it is the run's goal, Bribery refuses it
		// (V13) and a bonus dungeon is never it, so a city upgrade does not take the run's ending away. Issue #2838.
		TArray<int32> Counted;
		for (const FCataclysmDungeon& Dungeon : Dungeons)
		{
			if (Dungeon.CityId == CityId && Dungeon.Type != ECataclysmDungeonType::Quest
				&& Dungeon.Type != ECataclysmDungeonType::FallenCity
				&& Dungeon.Type != ECataclysmDungeonType::Cataclysm)
			{
				Counted.Add(Dungeon.DungeonId);
			}
		}

		// SOONEST TO RESOLVE FIRST AND ROUNDED, as `RemoveDungeons` takes them (ruling 3): half of three removes two,
		// half of one removes one.
		Counted.Sort(
			[this](const int32 Left, const int32 Right)
			{
				return Clock->DaysUntilResolveFor(Left) < Clock->DaysUntilResolveFor(Right);
			});
		const int32 ToRemove = FMath::Clamp(
			FMath::RoundToInt(Counted.Num() * UCataclysmCityUpgradeRules::CleanseDungeonShare), 0, Counted.Num());
		for (int32 Index = 0; Index < ToRemove; ++Index)
		{
			// REMOVED, NOT CLEARED: nobody walked them, so they pay nothing.
			RemoveDungeon(Counted[Index]);
		}

		// THEN HALF OF WHAT THE CITY HOLDS NOW: "The cities lose 50% of their remaining defenses and population". Of
		// what it is read as holding, which is never above the maximum with the empire upgrade tree. Not a dungeon's
		// hit, so no resistance takes any of it off.
		if (FCataclysmCity* City = Map->FindMutable(CityId))
		{
			City->Defence = Map->DefenceOf(*City) * (1.0f - UCataclysmCityUpgradeRules::CleanseLossShare);
			City->Population = Map->PopulationOf(*City) * (1.0f - UCataclysmCityUpgradeRules::CleanseLossShare);
		}
	}

	// AND STIMULUS PACKAGE'S FLAG, which this can set by itself (issue #2741 slice 6b, B2).
	NotePopulationAgainstHalf();
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

			// OF THE MAXIMA WITH THE EMPIRE UPGRADE TREE, from what the city is read as holding. Issue #2741 slice 3a.
			// AND THE UPGRADE'S VALUE AS READ TODAY, so Enduring Foundations strengthens one held long enough (3b).
			if (Upgrade.Effect == ECataclysmCityUpgradeEffect::HealDefenceEvery)
			{
				const float Max = Map->EffectiveMaxDefence(City);
				City.Defence = FMath::Min(Max, Map->DefenceOf(City) + Max * Map->UpgradeValueNow(Upgrade));
			}
			else
			{
				// AND CIVIC PRIDE'S SHARE, to the maximum (issue #2741 slice 9, V11).
				const float Max = Map->EffectiveMaxPopulation(City);
				City.Population = FMath::Min(Max, Map->PopulationOf(City)
													  + Max * TreeBonuses.PopulationRestoredShare(Map->UpgradeValueNow(Upgrade)));
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

	// WHICH TIER THIS RUN IS BEING PLAYED AT, AND WHO IT IS AGAINST. Until issue
	// #1444 the answer was always 1 and there was nothing to look at; now that
	// the tier comes from the game rather than from a default, a person needs to
	// be able to see which one a run took. This and the empire screens are the
	// only places a run is visible at all.
	//
	// THE CATACLYSMS ARE NAMED RATHER THAN COUNTED, because which ones a
	// character faces is drawn from its seed and is the thing a person would
	// otherwise have to re-derive to check.
	TArray<FString> Facing;
	for (const ECataclysmType Cataclysm : ActiveCataclysms)
	{
		Facing.Add(UCataclysmRoster::NameFor(Cataclysm).ToString());
	}

	Lines.Add(FString::Printf(
		TEXT("Difficulty tier %d, facing %d: %s."),
		DifficultyTier, ActiveCataclysms.Num(),
		Facing.IsEmpty() ? TEXT("nobody") : *FString::Join(Facing, TEXT(", "))));

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
			 "earned. %d ordinary dungeons have hit a city. %d Quest dungeon "
			 "timers have hit one."),
		DungeonsCleared, BasicDungeonsCleared, QuestObjectives,
		DungeonsDetonated, QuestHits));

	// THE BOSS DUNGEON, ONCE EARNED, which stands on no city and so is in no
	// line the map draws. Issue #1324 slice 6b.
	if (bCataclysmDungeonOpened)
	{
		Lines.Add(FString::Printf(
			TEXT("The Cataclysm boss dungeon opened on day %d: %d floors, %d "
				 "rolled and %d for the ordinary dungeons defeated. It stands "
				 "on no city."),
			CataclysmDungeonOpenedDay, CataclysmDungeonFloorsNow(),
			CataclysmDungeonBaseFloors,
			CataclysmDungeonFloorsNow() - CataclysmDungeonBaseFloors));
	}

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

			// AND WHAT ITS MODIFIERS ADD, WHICH NOTHING ELSE SHOWS ANYWHERE. A
			// dungeon's modifiers make every creature inside it worth more and
			// there is no screen that says so, so without this line the whole
			// feature is invisible outside its tests. Issue #41.
			//
			// A COUNT AND A NUMBER RATHER THAN THE NAMES. `FCataclysmDungeon::
			// Modifiers` holds row keys and turning one into `Edict of Silence`
			// needs the modifier DataTable, whose row type lives in the
			// `Cataclysm` module that this one must not depend on.
			// `UCataclysmDungeonModifierTable::NameOf` is that lookup, for a
			// caller that has both.
			//
			// NOTHING AT ALL WHEN IT CARRIES NONE, which is every dungeon in a
			// run whose modifier pool was never filled -- every headless test,
			// and the game itself if the DataTable cannot be read. A line
			// reading "0 modifiers" on every dungeon would be noise.
			const FString Modifiers = Dungeon.Modifiers.IsEmpty()
				? FString()
				: FString::Printf(TEXT(", %d modifiers worth %.0f danger"),
								  Dungeon.Modifiers.Num(),
								  Dungeon.ModifierScore);

			// A DUNGEON ON NO CITY HAS NO TIMER WORTH PRINTING: the Cataclysm
			// boss dungeon's is a billion days and never runs out
			// (`UCataclysmSurgeScheduler::NeverResolveDays`), and a billion
			// day countdown would read as a fault. Since 2026-11-02.
			const FString Where = City
				? FString::Printf(TEXT("on %s"), *City->Name)
				: FString(TEXT("on no city"));
			const FString Timer = Dungeon.CityId == INDEX_NONE
				? FString(TEXT("it never resolves"))
				: FString::Printf(TEXT("%.1f days until it resolves"),
								  Clock->DaysUntilResolveFor(Dungeon.DungeonId));

			Lines.Add(FString::Printf(
				TEXT("  dungeon %d%s %s: %d floors, %s%s"),
				Dungeon.DungeonId,
				*Marked,
				*Where,
				Dungeon.Floors,
				*Timer,
				*Modifiers));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}

void UCataclysmEmpireRun::NotePopulationAgainstHalf()
{
	// BELOW HALF NOW SETS IT; NOTHING HERE CLEARS IT (B2): a reclaim does, in `ClearDungeon`.
	const float Most = Map ? Map->TotalMaxPopulation() : 0.0f;
	if (Most > 0.0f && Map->TotalPopulation() < 0.5f * Most)
	{
		bPopulationFellBelowHalf = true;
	}
}
