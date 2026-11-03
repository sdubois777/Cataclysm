// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireMapLayout.h"

#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"

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

FString UCataclysmEmpireMapLayout::ProgressLine(const UCataclysmEmpireRun* Run)
{
	// A RUN THAT HAS NOT BEGUN SAYS NOTHING, rather than "no dungeons cleared".
	// The two states look the same in the counters and are not the same thing,
	// and a screen with no run already says so in its own words.
	if (Run == nullptr || Run->Map == nullptr)
	{
		return FString();
	}

	const int32 Cleared = Run->DungeonsCleared;
	const int32 Objectives = Run->QuestObjectives;

	// THE PLURALS ARE WRITTEN OUT because this line is read by a person and
	// "1 quest objectives" is the kind of thing that makes a screen look
	// unfinished. The rest of this screen has no number that can be one.
	const FString ClearedPart = Cleared == 0
		? FString(TEXT("No dungeons cleared yet"))
		: FString::Printf(TEXT("%d dungeon%s cleared"), Cleared,
						  Cleared == 1 ? TEXT("") : TEXT("s"));

	const FString ObjectivePart = Objectives == 0
		? FString(TEXT("no quest objectives"))
		: FString::Printf(TEXT("%d quest objective%s"), Objectives,
						  Objectives == 1 ? TEXT("") : TEXT("s"));

	// HOW FAR THE PLAYER IS FROM THE CATACLYSM BOSS DUNGEON, since issue #1324
	// slice 6c: the Cataclysms finished of those the run faces, and then what
	// opens it or that it has opened. Nothing when the run faces none, which no
	// begun run does: `Begin` always draws at least one.
	const int32 Facing = Run->ActiveCataclysms.Num();
	if (Facing <= 0)
	{
		return FString::Printf(TEXT("%s, %s"), *ClearedPart, *ObjectivePart);
	}

	const int32 Finished = Run->CataclysmsComplete();
	const int32 Needed = Run->CataclysmDungeonRequirement();

	FString BossPart;
	if (Run->bCataclysmDungeonOpened)
	{
		// ONLY A CLEAR TAKES IT OFF THE BOARD. `RemoveDungeon`'s other callers are
		// a resolve, which it never reaches, a city's fall and a city upgrade,
		// which both ask `DungeonsOn` a real city and never list it.
		BossPart = Run->FindCataclysmDungeon() != nullptr
			? FString(TEXT("the Cataclysm boss dungeon is open"))
			: FString(TEXT("the Cataclysm boss dungeon has been cleared"));
	}
	else if (Finished >= Needed)
	{
		// MET AND NOT OPENED: a run record from before version 4. See the header.
		BossPart = TEXT("the boss dungeon opens on the next quest dungeon cleared");
	}
	else
	{
		BossPart = FString::Printf(TEXT("the boss dungeon opens when %d %s finished"),
								   Needed, Needed == 1 ? TEXT("is") : TEXT("are"));
	}

	return FString::Printf(TEXT("%s, %s, %d of %d Cataclysm%s finished; %s"),
						   *ClearedPart, *ObjectivePart, Finished, Facing,
						   Facing == 1 ? TEXT("") : TEXT("s"), *BossPart);
}

FString UCataclysmEmpireMapLayout::DaysOwedLine(const UCataclysmEmpireRun* Run)
{
	const int32 Owed = Run != nullptr ? Run->DaysOwed() : 0;
	if (Owed <= 0)
	{
		return FString();
	}

	return Owed == 1
		? FString(TEXT("1 day passes when you leave"))
		: FString::Printf(TEXT("%d days pass when you leave"), Owed);
}

FString UCataclysmEmpireMapLayout::LastHitLine(const UCataclysmEmpireRun* Run)
{
	// NOTHING TO SAY: no run, a run that has not begun, or no hit and no move
	// yet. Day 0 is what `UCataclysmEmpireRun::Begin` writes, and no day of a
	// run is day 0.
	if (Run == nullptr || Run->Map == nullptr
		|| (Run->LastHitDay <= 0 && Run->LastMoveDay <= 0))
	{
		return FString();
	}

	// A FALL'S MOVE OF A QUEST DUNGEON, WHEN IT IS AT LEAST AS RECENT AS THE
	// LAST HIT. Since 2026-10-21 a Quest dungeon on a city that falls moves
	// rather than being absorbed, and without this line a player sees the city
	// lost and a Quest dungeon standing somewhere it was not. The later of the
	// two is shown; ON THE SAME DAY THE MOVE IS SHOWN, because a fall that
	// moved a dungeon is the larger event and the hit that felled the city is
	// usually the same story. The line the console prints for the day names
	// both.
	if (Run->LastMoveDay > 0 && Run->LastMoveDay >= Run->LastHitDay)
	{
		const FCataclysmCity* From = Run->Map->Find(Run->LastMoveFromCityId);
		const FCataclysmCity* To = Run->Map->Find(Run->LastMoveToCityId);
		const FString FromName = From != nullptr
			? From->Name : FString(TEXT("a city"));
		const FString ToName = To != nullptr
			? To->Name : FString(TEXT("a city"));

		// THE CITY THAT FELL IS THE CITY IT MOVED FROM, named twice so the
		// sentence says both where it was and why it left.
		return FString::Printf(
			TEXT("Day %d: a Quest dungeon moved from %s to %s because %s fell."),
			Run->LastMoveDay, *FromName, *ToName, *FromName);
	}

	// THE CITY IS NAMED AS THE MAP NAMES IT. A number nobody can look up says
	// nothing, and a city that cannot be found is still a hit worth a line.
	const FCataclysmCity* HitCity = Run->Map->Find(Run->LastHitCityId);
	const FString HitCityName = HitCity != nullptr
		? HitCity->Name : FString(TEXT("a city"));

	return Run->bLastHitWasAQuestTimer
		? FString::Printf(TEXT("Day %d: a Quest dungeon's timer hit %s."),
						  Run->LastHitDay, *HitCityName)
		: FString::Printf(TEXT("Day %d: a dungeon hit %s and left."),
						  Run->LastHitDay, *HitCityName);
}

FString UCataclysmEmpireMapLayout::LastDeathLine(const UCataclysmEmpireRun* Run)
{
	// NOTHING TO SAY: no run, a run that has not begun, or no death yet. Day 0 is what `Begin` writes, and no death
	// is on day 0: a run begins with nothing standing on the map, and the first wave lands on day 1.
	if (Run == nullptr || Run->Map == nullptr || Run->LastDeathDay <= 0)
	{
		return FString();
	}

	const FString Where = Run->LastDeathDungeonName.IsEmpty()
		? FString(TEXT("a dungeon")) : Run->LastDeathDungeonName;

	// THE WALK'S DAYS, AND THEN THE DEATH'S ON TOP OF THEM. "More" only when the walk ran some, so a walk that owed
	// no whole day does not read as if the death's days were added to something.
	const int32 Walk = Run->LastDeathWalkDays;
	const int32 Cost = Run->LastDeathDaysCost;
	const FString WalkPart = Walk <= 0
		? FString(TEXT("The walk owed no whole day"))
		: FString::Printf(TEXT("The walk's %d day%s ran"), Walk, Walk == 1 ? TEXT("") : TEXT("s"));
	const FString CostPart = FString::Printf(TEXT("the death cost %d %sday%s"), Cost,
											 Walk > 0 ? TEXT("more ") : TEXT(""), Cost == 1 ? TEXT("") : TEXT("s"));

	// EVERY CITY THE DEATH'S RESOLVE FELLED, BY THE MAP'S NAME FOR IT, joined as a sentence joins a list. A city that
	// cannot be found is still a city that fell.
	TArray<FString> Names;
	for (const int32 CityId : Run->LastDeathFallenCityIds)
	{
		const FCataclysmCity* City = Run->Map->Find(CityId);
		Names.Add(City != nullptr ? City->Name : FString(TEXT("a city")));
	}
	FString FellPart;
	if (Names.IsEmpty())
	{
		FellPart = TEXT("no city fell");
	}
	else if (Names.Num() == 1)
	{
		FellPart = FString::Printf(TEXT("%s fell"), *Names[0]);
	}
	else
	{
		const FString Last = Names.Pop();
		FellPart = FString::Printf(TEXT("%s and %s fell"), *FString::Join(Names, TEXT(", ")), *Last);
	}

	return FString::Printf(TEXT("Day %d: you died in %s. %s, %s, and %s."),
						   Run->LastDeathDay, *Where, *WalkPart, *CostPart, *FellPart);
}

FString UCataclysmEmpireMapLayout::LatestEventLine(const UCataclysmEmpireRun* Run)
{
	// THE DAY THE BOSS DUNGEON OPENED, or 0 while it has not, so a run without one compares as it did before issue
	// #1324 slice 6c: no death or hit is on a day below 0.
	const int32 OpenedDay = Run != nullptr && Run->bCataclysmDungeonOpened ? Run->CataclysmDungeonOpenedDay : 0;

	// THE DEATH WHILE NOTHING ELSE WITH A LINE IS MORE RECENT; on the same day, the death. See the header.
	if (Run != nullptr && Run->LastDeathDay > 0
		&& Run->LastDeathDay >= Run->LastHitDay && Run->LastDeathDay >= Run->LastMoveDay
		&& Run->LastDeathDay >= OpenedDay)
	{
		const FString Death = LastDeathLine(Run);
		if (!Death.IsEmpty())
		{
			return Death;
		}
	}

	// THEN THE OPENING WHILE NO HIT OR MOVE IS MORE RECENT; on the same day, the opening, which came after them.
	if (Run != nullptr && Run->bCataclysmDungeonOpened
		&& OpenedDay >= Run->LastHitDay && OpenedDay >= Run->LastMoveDay)
	{
		const FString Opened = CataclysmDungeonOpenedLine(Run);
		if (!Opened.IsEmpty())
		{
			return Opened;
		}
	}
	return LastHitLine(Run);
}

// ---------------------------------------------------------------------------
// The Cataclysm boss dungeon, which stands on no city -- issue #1324 slice 6c
// ---------------------------------------------------------------------------

FVector2D UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(float Scale)
{
	// THE CENTRE, A MARGIN IN FROM THE TOP AND THE LEFT. The margin is not scaled, as `ScaleToFit`'s is not, so the
	// marker and the diamond keep the same clear edge. See the header for why no city can be under it.
	const FVector2D Size = CataclysmDungeonMarkerSize(Scale);
	return FVector2D(FitMarginPx, FitMarginPx) + Size * 0.5;
}

FVector2D UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(float Scale)
{
	return FVector2D(CataclysmDungeonMarkerWidthPx * Scale, CataclysmDungeonMarkerHeightPx * Scale);
}

FString UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel(const UCataclysmEmpireRun* Run)
{
	const FCataclysmDungeon* Boss = Run != nullptr && Run->Map != nullptr ? Run->FindCataclysmDungeon() : nullptr;
	if (Boss == nullptr)
	{
		return FString();
	}

	// THE DEPTH ON THE BOX, because it is the one figure about it that changes: every ordinary dungeon defeated adds
	// a floor. One floor is not a depth a boss rolls, but a dungeon built by hand can have it.
	return FString::Printf(TEXT("Cataclysm boss: %d floor%s"), Boss->Floors, Boss->Floors == 1 ? TEXT("") : TEXT("s"));
}

FString UCataclysmEmpireMapLayout::CataclysmDungeonBrief(const UCataclysmEmpireRun* Run)
{
	const FCataclysmDungeon* Boss = Run != nullptr && Run->Map != nullptr ? Run->FindCataclysmDungeon() : nullptr;
	if (Boss == nullptr)
	{
		return FString();
	}

	// WHO SENT IT AND WHAT KIND IT IS, each left out when it says nothing: `None` has no Cataclysm to name and a
	// dungeon with no sub-type has none to show.
	TArray<FString> Marks;
	if (Boss->Cataclysm != ECataclysmType::None)
	{
		Marks.Add(UCataclysmRoster::NameFor(Boss->Cataclysm).ToString());
	}
	const FString SubType = UCataclysmSurgeScheduler::SubTypeName(Boss->SubType);
	if (!SubType.IsEmpty())
	{
		Marks.Add(SubType);
	}
	const FString Marked = Marks.IsEmpty()
		? FString()
		: FString::Printf(TEXT(" (%s)"), *FString::Join(Marks, TEXT(", ")));

	// THE FLOORS THE ORDINARY DUNGEONS ADDED, which are the dungeon's floors less the ones it rolled. Never below
	// none, whatever a hand-built run holds.
	const int32 Added = FMath::Max(0, Boss->Floors - Run->CataclysmDungeonBaseFloors);
	FString AddedPart;
	if (Added == 0)
	{
		AddedPart = TEXT("as it rolled");
	}
	else if (Added == 1)
	{
		AddedPart = TEXT("1 of them added by an ordinary dungeon defeated");
	}
	else
	{
		AddedPart = FString::Printf(TEXT("%d of them added by ordinary dungeons defeated"), Added);
	}

	// AND HOW TO WALK IT, because pressing its box cannot: nothing enters a dungeon from this map (issue #48), so the
	// line names the console command that does, with this dungeon's identifier. `Cataclysm.EnterDungeon` is the one
	// registered in `CataclysmPlayerCharacter.cpp`; `test_a_message_names_only_registered_console_commands.py` holds
	// that every `Cataclysm.` name in a message here is registered.
	return FString::Printf(
		TEXT("The Cataclysm boss dungeon%s: %d floor%s, %s. Every ordinary dungeon defeated adds a floor. It stands on "
			 "no city and never resolves. To walk it, type Cataclysm.EnterDungeon %d in a dungeon level; nothing "
			 "enters a dungeon from this map yet."),
		*Marked, Boss->Floors, Boss->Floors == 1 ? TEXT("") : TEXT("s"), *AddedPart, Boss->DungeonId);
}

FString UCataclysmEmpireMapLayout::CataclysmDungeonOpenedLine(const UCataclysmEmpireRun* Run)
{
	if (Run == nullptr || Run->Map == nullptr || !Run->bCataclysmDungeonOpened)
	{
		return FString();
	}

	return FString::Printf(TEXT("Day %d: the Cataclysm boss dungeon opened."), Run->CataclysmDungeonOpenedDay);
}

FString UCataclysmEmpireMapLayout::LastRunWonLine(const UCataclysmEmpireRun* Run)
{
	// NOTHING TO SAY: no run, a run that has not begun, or no win before it. Day 0 is what `Begin` writes, and no run
	// is won on day 0: the first wave lands on day 1. Issue #1324 slice 6e.
	if (Run == nullptr || Run->Clock == nullptr || Run->LastRunWonDay <= 0)
	{
		return FString();
	}

	// THE CATACLYSM AS THE DESIGN NAMES IT: "The Void" with its article, the other seven by their names.
	const FString Against = Run->LastRunWonCataclysm == ECataclysmType::Void
		? FString(TEXT("The Void"))
		: UCataclysmRoster::NameFor(Run->LastRunWonCataclysm).ToString();

	// THE POINTS, OR WHY THERE WERE NONE: a boss defeated before pays nothing (ruling R4).
	const FString PointsPart = Run->LastRunWonPassivePoints > 0
		? FString::Printf(TEXT("+%d passive points."), Run->LastRunWonPassivePoints)
		: FString::Printf(TEXT("%s's boss was defeated before, so no passive points."), *Against);

	// WHAT THIS RUN FACES. One more than the won run, except at the top of the ladder, where it is the same eight.
	const int32 Faces = Run->ActiveCataclysms.Num();
	const FString FacesPart = Faces <= Run->LastRunWonTier
		? FString::Printf(TEXT("This run faces the same %d Cataclysms: tier %d is the highest."), Faces,
						  Run->LastRunWonTier)
		: FString::Printf(TEXT("This run faces %d Cataclysm%s."), Faces, Faces == 1 ? TEXT("") : TEXT("s"));

	return FString::Printf(TEXT("Run won on day %d at tier %d against %s. %s %s"), Run->LastRunWonDay,
						   Run->LastRunWonTier, *Against, *PointsPart, *FacesPart);
}
